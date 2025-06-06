// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkGmshWriter.h"
#include "gmshCommon.h"

#include "vtkAlgorithm.h"
#include "vtkArrayDispatch.h"
#include "vtkCellData.h"
#include "vtkCellType.h"
#include "vtkCellTypes.h"
#include "vtkCharArray.h"
#include "vtkDataArray.h"
#include "vtkDataSetAttributes.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkLongArray.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkShortArray.h"
#include "vtkStreamingDemandDrivenPipeline.h"
#include "vtkType.h"
#include "vtkUnsignedCharArray.h"
#include "vtkUnsignedIntArray.h"
#include "vtkUnsignedLongArray.h"
#include "vtkUnsignedShortArray.h"
#include "vtkUnstructuredGrid.h"

#include "gmsh.h"

#include <numeric>
#include <set>
#include <unordered_map>
#include <vector>

//-----------------------------------------------------------------------------
struct GmshWriterInternal
{
  int Dimension = 3;
  int ModelTag = -1;
  std::vector<std::string> NodeViews;
  std::vector<std::string> CellViews;
  std::string ModelName;

  std::vector<std::size_t> CellDataIndex;

  double* TimeSteps = nullptr;
  unsigned int NumberOfTimeSteps = 0;
  unsigned int CurrentTimeStep = 0;
  double CurrentTime = 0.0;

  vtkUnstructuredGrid* Input = nullptr;

  static const std::unordered_map<unsigned char, GmshPrimitive> TRANSLATE_CELLS_TYPE;
  static constexpr unsigned char MAX_TAG = 15u;

  vtkSmartPointer<vtkDataArray> EntityIDs = nullptr;

  using InverseMap = std::map<std::pair<std::size_t, std::size_t>, std::vector<int>>;
  InverseMap InverseEntityMapping;
  std::unordered_map<int, std::unordered_map<vtkIdType, std::size_t>> VtkGmshNodeMap;
};

const std::unordered_map<unsigned char, GmshPrimitive> GmshWriterInternal::TRANSLATE_CELLS_TYPE = {
  { VTK_VERTEX, GmshPrimitive::POINT }, { VTK_LINE, GmshPrimitive::LINE },
  { VTK_POLY_LINE, GmshPrimitive::Unsupported }, { VTK_TRIANGLE, GmshPrimitive::TRIANGLE },
  { VTK_TRIANGLE_STRIP, GmshPrimitive::Unsupported }, { VTK_POLYGON, GmshPrimitive::Unsupported },
  { VTK_PIXEL, GmshPrimitive::QUAD }, { VTK_QUAD, GmshPrimitive::QUAD },
  { VTK_TETRA, GmshPrimitive::TETRA }, { VTK_VOXEL, GmshPrimitive::HEXA },
  { VTK_HEXAHEDRON, GmshPrimitive::HEXA }, { VTK_WEDGE, GmshPrimitive::PRISM },
  { VTK_PYRAMID, GmshPrimitive::PYRAMID }
};

//-----------------------------------------------------------------------------
namespace
{
// Reorder voxel vertices as hexahedron vertices to support it with Gmsh.
// `cellArray` contains a lot a cell vertices, while `idx` is the index of the
// first vertex for the current voxel
void OrderVoxel(std::vector<std::size_t>& cellArray, std::size_t idx)
{
  std::swap(cellArray[idx + 2], cellArray[idx + 3]);
  std::swap(cellArray[idx + 6], cellArray[idx + 7]);
}

// Reorder pixel vertices as quad vertices to support it with Gmsh.
// `cellArray` contains a lot a cell vertices, while `idx` is the index of the
// first vertex for the current pixel
void OrderPixel(std::vector<std::size_t>& cellArray, std::size_t idx)
{
  std::swap(cellArray[idx + 2], cellArray[idx + 3]);
}

// Transform VTK types not supported by Gmsh (like triange strips or polylines)
// as simpler primitives.
void AddTriangulatedCell(std::vector<std::size_t>& nodeTags,
  std::vector<std::size_t>& newTypeIndexes, const std::vector<std::size_t>& oldTypeIndexes,
  GmshWriterInternal* internal, vtkIdType& cellCounterId, const int dim)
{
  for (std::size_t idx : oldTypeIndexes)
  {
    const std::size_t vtkIdx = idx - 1u;
    vtkCell* cell = internal->Input->GetCell(vtkIdx);
    vtkNew<vtkIdList> ptIds;
    vtkNew<vtkPoints> points;
    cell->Triangulate(0, ptIds, points);
    const vtkIdType numIds = ptIds->GetNumberOfIds();

    for (vtkIdType i = 0; i < numIds; ++i)
    {
      nodeTags.push_back(ptIds->GetId(i) + 1);
    }
    for (int i = 0; i < (numIds / dim); ++i)
    {
      newTypeIndexes.push_back(cellCounterId);
      internal->CellDataIndex.push_back(vtkIdx);
      ++cellCounterId;
    }
  }
}

// Add every supported VTK cells in the gmsh model by type.
void FillCells(const int entityTag, GmshWriterInternal* internal,
  std::vector<std::size_t> idxPerType[], vtkDataArray* offsets, vtkDataArray* connectivity,
  vtkIdType& cellCounterId)
{

  for (unsigned char currentType = 1; currentType < GmshWriterInternal::MAX_TAG; ++currentType)
  {
    std::vector<std::size_t>& indexes = idxPerType[currentType];
    if (indexes.empty())
    {
      continue;
    }

    signed char gmshType =
      static_cast<signed char>(GmshWriterInternal::TRANSLATE_CELLS_TYPE.find(currentType)->second);
    // If this type is not natively supported, it will be transleted in either lines or triangles
    if (gmshType < 0)
    {
      continue;
    }

    // Add cells for this vtk type
    std::vector<std::size_t> gmshNodeTags;
    for (std::size_t idx : indexes)
    {
      const std::size_t vtkIdx = idx - 1;
      const int beginCell = gmshNodeTags.size();
      long offsetBegin = static_cast<long>(*offsets->GetTuple(vtkIdx));
      long offsetEnd = static_cast<long>(*offsets->GetTuple(vtkIdx + 1));
      for (unsigned int j = offsetBegin; j < offsetEnd; ++j)
      {
        gmshNodeTags.emplace_back(internal->VtkGmshNodeMap[entityTag][*connectivity->GetTuple(j)]);
      }

      // Ordering if needed
      if (currentType == VTK_PIXEL)
      {
        ::OrderPixel(gmshNodeTags, beginCell);
      }
      else if (currentType == VTK_VOXEL)
      {
        ::OrderVoxel(gmshNodeTags, beginCell);
      }

      // Add data index
      internal->CellDataIndex.push_back(vtkIdx);
    }

    // Generate gmsh ids
    std::vector<std::size_t> gmshIds(indexes.size());
    std::iota(gmshIds.begin(), gmshIds.end(), cellCounterId);
    cellCounterId += gmshIds.size();

    // Translate non-supported type into supported one
    if (currentType == VTK_LINE)
    {
      ::AddTriangulatedCell(
        gmshNodeTags, gmshIds, idxPerType[VTK_POLY_LINE], internal, cellCounterId, 2);
    }
    else if (currentType == VTK_TRIANGLE)
    {
      ::AddTriangulatedCell(
        gmshNodeTags, gmshIds, idxPerType[VTK_TRIANGLE_STRIP], internal, cellCounterId, 3);
      ::AddTriangulatedCell(
        gmshNodeTags, gmshIds, idxPerType[VTK_POLYGON], internal, cellCounterId, 3);
    }

    gmsh::model::mesh::addElementsByType(entityTag, gmshType, gmshIds, gmshNodeTags);
  }
}

struct IntegralChecker
{
  template <template <typename> class ArrayT, typename VType>
  void operator()(ArrayT<VType>*)
  {
  }
};

bool CheckIntegralDataArray(vtkDataArray* dArr)
{
  if (!dArr)
  {
    return false;
  }

  using IntegralTL = vtkArrayDispatch::Integrals;
  using Dispatcher = vtkArrayDispatch::DispatchByValueType<IntegralTL>;
  return Dispatcher::Execute(dArr, IntegralChecker());
}

vtkDataArray* GetIntegralArray(vtkUnstructuredGrid* input, char* name)
{
  if (name && std::string(name) != "None")
  {
    vtkDataArray* entityIDs = input->GetCellData()->GetArray(name);
    if (!entityIDs)
    {
      vtkWarningWithObjectMacro(nullptr, "Could not find " << name << " in data set");
      return nullptr;
    }
    if (!::CheckIntegralDataArray(entityIDs))
    {
      vtkWarningWithObjectMacro(nullptr, "Array " << name << " is not of integral type");
      return nullptr;
    }
    return entityIDs;
  }
  return nullptr;
}

void AddInverseMapping(
  vtkUnstructuredGrid* input, vtkDataArray* mapping, GmshWriterInternal::InverseMap& invMap)
{
  vtkUnsignedCharArray* cellTypes = input->GetCellTypesArray();
  auto cTypeRange = vtk::DataArrayValueRange<1>(cellTypes);
  auto mappingRange = vtk::DataArrayValueRange<1>(mapping);
  auto cTypeIt = cTypeRange.begin();
  auto mapIt = mappingRange.begin();
  for (vtkIdType iCell = 0; iCell < input->GetNumberOfCells(); ++iCell)
  {
    std::pair<std::size_t, std::size_t> dimEnt = { vtkCellTypes::GetDimension(*cTypeIt), *mapIt };
    mapIt++, cTypeIt++;
    auto it = invMap.find(dimEnt);
    if (it == invMap.end())
    {
      invMap.emplace(dimEnt, std::vector<int>({ static_cast<int>(iCell) }));
      continue;
    }
    it->second.emplace_back(iCell);
  }
}
}

VTK_ABI_NAMESPACE_BEGIN
//-----------------------------------------------------------------------------
vtkStandardNewMacro(vtkGmshWriter);

//-----------------------------------------------------------------------------
vtkGmshWriter::vtkGmshWriter()
  : Internal(new GmshWriterInternal)
{
  this->SetNumberOfInputPorts(1);
}

//-----------------------------------------------------------------------------
vtkGmshWriter::~vtkGmshWriter()
{
  this->SetFileName(nullptr);
  delete this->Internal;
}

//-----------------------------------------------------------------------------
void vtkGmshWriter::SetUpEntities()
{

  vtkSmartPointer<vtkDataArray> entityIDs =
    ::GetIntegralArray(this->Internal->Input, this->ElementaryEntityIDFieldName);
  if (!entityIDs)
  {
    if (vtkDataArray* physIDs =
          ::GetIntegralArray(this->Internal->Input, this->PhysicalGroupIDFieldName))
    {
      entityIDs = physIDs;
    }
    else
    {
      // create entityIDs
      entityIDs = vtkSmartPointer<vtkIntArray>::New();
      entityIDs->SetName("gmshEntityId");
      entityIDs->SetNumberOfComponents(1);
      entityIDs->SetNumberOfTuples(this->Internal->Input->GetNumberOfCells());
      vtkUnsignedCharArray* cellType = this->Internal->Input->GetCellTypesArray();
      for (vtkIdType iCell = 0; iCell < this->Internal->Input->GetNumberOfCells(); ++iCell)
      {
        entityIDs->SetComponent(
          iCell, 0, vtkCellTypes::GetDimension(cellType->GetValue(iCell)) + 1);
      }
    }
  }

  this->Internal->EntityIDs = entityIDs;

  ::AddInverseMapping(this->Internal->Input, entityIDs, this->Internal->InverseEntityMapping);
  for (auto pair : this->Internal->InverseEntityMapping)
  {
    gmsh::model::addDiscreteEntity(pair.first.first, pair.first.second);
  }
}

//-----------------------------------------------------------------------------
bool vtkGmshWriter::SetUpPhysicalGroups()
{
  if (!this->Internal->EntityIDs)
  {
    vtkErrorMacro("Cannot setup physical groups before setting up entities");
    return false;
  }
  vtkDataArray* physIDs = ::GetIntegralArray(this->Internal->Input, this->PhysicalGroupIDFieldName);
  if (physIDs)
  {
    // generate physical groups
    GmshWriterInternal::InverseMap invPhysical;
    ::AddInverseMapping(this->Internal->Input, physIDs, invPhysical);
    for (auto pair : invPhysical)
    {
      std::transform(pair.second.begin(), pair.second.end(), pair.second.begin(),
        [&](int val) { return static_cast<int>(this->Internal->EntityIDs->GetComponent(val, 0)); });
      std::set<int> uniques(pair.second.begin(), pair.second.end());
      pair.second.assign(uniques.begin(), uniques.end());
      gmsh::model::addPhysicalGroup(pair.first.first, pair.second, pair.first.second);
    }
  }
  return true;
}

//-----------------------------------------------------------------------------
void vtkGmshWriter::LoadNodes()
{
  this->Internal->VtkGmshNodeMap.clear();
  vtkUnstructuredGrid* input = this->Internal->Input;

  vtkDataArray* offsets = input->GetCells()->GetOffsetsArray();
  vtkDataArray* connectivity = input->GetCells()->GetConnectivityArray();
  vtkDataArray* points = input->GetPoints()->GetData();

  std::size_t runningNodeTag = 1u;
  for (auto pair : this->Internal->InverseEntityMapping)
  {

    std::vector<vtkIdType> vtkTags;
    for (vtkIdType iCell : pair.second)
    {
      int offsetBegin = offsets->GetComponent(iCell, 0);
      int offsetEnd = offsets->GetComponent(iCell + 1, 0);
      int cellSize = offsetEnd - offsetBegin;
      vtkTags.resize(vtkTags.size() + cellSize);
      for (vtkIdType iP = offsetBegin; iP < offsetEnd; iP++)
      {
        vtkTags.emplace_back(connectivity->GetComponent(iP, 0));
      }
    }

    {
      std::set<vtkIdType> uniques(vtkTags.begin(), vtkTags.end());
      vtkTags.assign(uniques.begin(), uniques.end());
    }

    int counter = 0;
    std::vector<double> inlineCoords(vtkTags.size() * 3, 0.0);
    for (vtkIdType globPIndex : vtkTags)
    {
      for (vtkIdType j = 0; j < 3; ++j)
      {
        inlineCoords[counter] = *(points->GetTuple(globPIndex) + j);
        counter++;
      }
    }

    std::vector<std::size_t> gmshTags(vtkTags.size());
    std::iota(gmshTags.begin(), gmshTags.end(), runningNodeTag);
    runningNodeTag += vtkTags.size();

    this->Internal->VtkGmshNodeMap[pair.first.second] =
      std::unordered_map<vtkIdType, std::size_t>();
    auto vtkIt = vtkTags.begin();
    auto gmshIt = gmshTags.begin();
    for (; vtkIt != vtkTags.end(); ++vtkIt, ++gmshIt)
    {
      this->Internal->VtkGmshNodeMap[pair.first.second][*vtkIt] = *gmshIt;
    }

    gmsh::model::mesh::addNodes(pair.first.first, pair.first.second, gmshTags, inlineCoords);
  }
}

//-----------------------------------------------------------------------------
void vtkGmshWriter::LoadCells()
{
  vtkUnstructuredGrid* input = this->Internal->Input;

  // Build list of gmsh indexes per type
  vtkCellArray* cells = input->GetCells();
  vtkUnsignedCharArray* cellType = input->GetCellTypesArray();
  vtkIdType cellCounterId = 1;

  this->Internal->CellDataIndex.clear();
  this->Internal->CellDataIndex.reserve(input->GetNumberOfCells());

  for (auto pair : this->Internal->InverseEntityMapping)
  {
    std::vector<std::size_t> indexesPerTypes[GmshWriterInternal::MAX_TAG];
    for (int iCell : pair.second)
    {
      const unsigned char vtkCellType = cellType->GetValue(iCell);
      if (!GmshWriterInternal::TRANSLATE_CELLS_TYPE.count(vtkCellType))
      {
        continue;
      }
      indexesPerTypes[vtkCellType].push_back(static_cast<std::size_t>(iCell + 1));
    }

    // Add these cells to gmsh::model
    ::FillCells(pair.first.second, this->Internal, indexesPerTypes, cells->GetOffsetsArray(),
      cells->GetConnectivityArray(), cellCounterId);
  }
}

//-----------------------------------------------------------------------------
void vtkGmshWriter::LoadNodeData()
{
  vtkDataSetAttributes* pointData =
    vtkDataSetAttributes::SafeDownCast(this->Internal->Input->GetPointData());
  const int numVtkArrays = this->Internal->NodeViews.size();
  if (numVtkArrays == 0)
  {
    return;
  }

  // Generate Gmsh tags
  std::size_t totNodeTags = 0;
  std::for_each(this->Internal->VtkGmshNodeMap.begin(), this->Internal->VtkGmshNodeMap.end(),
    [&totNodeTags](std::pair<std::size_t, std::unordered_map<vtkIdType, std::size_t>> p)
    { totNodeTags += p.second.size(); });
  std::vector<std::size_t> tags(totNodeTags);
  std::iota(tags.begin(), tags.end(), 1);

  for (int arrayId = 0; arrayId < numVtkArrays; ++arrayId)
  {
    // Get VTK data (we can safely safedowncast because we already checked in InitViews())
    const std::string arrayName = this->Internal->NodeViews[arrayId];
    vtkDataArray* vtkArray =
      vtkDataArray::SafeDownCast(pointData->GetAbstractArray(arrayName.c_str()));
    const int numComponents = vtkArray->GetNumberOfComponents();

    // Store it in a structure Gmsh can understand
    std::vector<double> gmshData(totNodeTags * numComponents, 0.0);
    for (auto ent : this->Internal->VtkGmshNodeMap)
    {
      for (auto nodeTags : ent.second)
      {
        for (int j = 0; j < numComponents; ++j)
        {
          gmshData[(nodeTags.second - 1) * numComponents + j] =
            *(vtkArray->GetTuple(nodeTags.first) + j);
        }
      }
    }

    // Finally add it to gmsh
    gmsh::view::addHomogeneousModelData(arrayId, this->Internal->CurrentTimeStep,
      this->Internal->ModelName, "NodeData", tags, gmshData, this->Internal->CurrentTime,
      numComponents);
  }
}

//-----------------------------------------------------------------------------
void vtkGmshWriter::LoadCellData()
{
  vtkDataSetAttributes* cellData =
    vtkDataSetAttributes::SafeDownCast(this->Internal->Input->GetCellData());
  const int numVtkArrays = this->Internal->CellViews.size();
  if (numVtkArrays == 0)
  {
    return;
  }

  const int vtkArrayOffset = this->Internal->NodeViews.size();
  // Generate Gmsh tags
  std::vector<std::size_t> tags(this->Internal->CellDataIndex.size());
  std::iota(tags.begin(), tags.end(), 1);

  for (int arrayId = 0; arrayId < numVtkArrays; ++arrayId)
  {
    // Get VTK data (we can safely safedowncast because we already checked in InitViews())
    std::string arrayName = this->Internal->CellViews[arrayId];
    vtkDataArray* vtkArray =
      vtkDataArray::SafeDownCast(cellData->GetAbstractArray(arrayName.c_str()));
    const int numComponents = vtkArray->GetNumberOfComponents();

    // Store it in a structure Gmsh can understand
    std::vector<double> gmshData(this->Internal->CellDataIndex.size() * numComponents);
    vtkIdType counter = 0;
    for (std::size_t idx : this->Internal->CellDataIndex)
    {
      for (int j = 0; j < numComponents; ++j)
      {
        gmshData[counter] = *(vtkArray->GetTuple(idx) + j);
        ++counter;
      }
    }

    // Finally add it to gmsh
    gmsh::view::addHomogeneousModelData(arrayId + vtkArrayOffset, this->Internal->CurrentTimeStep,
      this->Internal->ModelName, "ElementData", tags, gmshData, this->Internal->CurrentTime,
      numComponents);
  }
}

//-----------------------------------------------------------------------------
int vtkGmshWriter::ProcessRequest(
  vtkInformation* request, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  if (request->Has(vtkDemandDrivenPipeline::REQUEST_INFORMATION()))
  {
    return this->RequestInformation(request, inputVector, outputVector);
  }
  else if (request->Has(vtkStreamingDemandDrivenPipeline::REQUEST_UPDATE_EXTENT()))
  {
    return this->RequestUpdateExtent(request, inputVector, outputVector);
  }
  else if (request->Has(vtkDemandDrivenPipeline::REQUEST_DATA()))
  {
    return this->RequestData(request, inputVector, outputVector);
  }

  return this->Superclass::ProcessRequest(request, inputVector, outputVector);
}

//-----------------------------------------------------------------------------
int vtkGmshWriter::RequestInformation(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector*)
{
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  if (this->WriteAllTimeSteps && inInfo->Has(vtkStreamingDemandDrivenPipeline::TIME_STEPS()))
  {
    this->Internal->NumberOfTimeSteps =
      inInfo->Length(vtkStreamingDemandDrivenPipeline::TIME_STEPS());
  }
  else
  {
    this->Internal->NumberOfTimeSteps = 0;
  }
  this->Internal->CurrentTimeStep = 0;

  return 1;
}

//-----------------------------------------------------------------------------
int vtkGmshWriter::RequestUpdateExtent(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector*)
{
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  if (this->WriteAllTimeSteps && inInfo->Has(vtkStreamingDemandDrivenPipeline::TIME_STEPS()))
  {
    double* timeSteps = inInfo->Get(vtkStreamingDemandDrivenPipeline::TIME_STEPS());
    this->Internal->CurrentTime = timeSteps[this->Internal->CurrentTimeStep];
    inputVector[0]->GetInformationObject(0)->Set(
      vtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP(), this->Internal->CurrentTime);
  }
  return 1;
}

//-----------------------------------------------------------------------------
void vtkGmshWriter::InitViews()
{
  vtkDataSetAttributes* pointsData =
    vtkDataSetAttributes::SafeDownCast(this->Internal->Input->GetPointData());

  int arrayCounter = 0;
  std::string name;
  const int nbPointArrays = pointsData->GetNumberOfArrays();
  for (int i = 0; i < nbPointArrays; ++i)
  {
    name = pointsData->GetArrayName(i);
    if (name.rfind("gmsh", 0) == 0 && !this->WriteGmshSpecificArray)
    {
      continue;
    }
    vtkDataArray* vtkArray = vtkDataArray::SafeDownCast(pointsData->GetAbstractArray(name.c_str()));
    if (!vtkArray)
    {
      continue;
    }
    this->Internal->NodeViews.push_back(name);
    gmsh::view::add(name, arrayCounter);
    ++arrayCounter;
  }

  vtkDataSetAttributes* cellsData =
    vtkDataSetAttributes::SafeDownCast(this->Internal->Input->GetCellData());
  const int nbCellArrays = cellsData->GetNumberOfArrays();
  for (int i = 0; i < nbCellArrays; ++i)
  {
    name = cellsData->GetArrayName(i);
    if (name.rfind("gmsh", 0) == 0 && !this->WriteGmshSpecificArray)
    {
      continue;
    }
    vtkDataArray* vtkArray = vtkDataArray::SafeDownCast(cellsData->GetAbstractArray(name.c_str()));
    if (!vtkArray)
    {
      continue;
    }
    this->Internal->CellViews.push_back(name);
    gmsh::view::add(name, arrayCounter);
    ++arrayCounter;
  }
}

//-----------------------------------------------------------------------------
int vtkGmshWriter::RequestData(
  vtkInformation* request, vtkInformationVector**, vtkInformationVector*)
{
  // make sure the user specified a FileName
  if (!this->FileName)
  {
    vtkErrorMacro(<< "Please specify FileName to use");
    return 0;
  }

  this->Internal->Input = this->GetInput();

  // If this is the first request
  if (this->Internal->CurrentTimeStep == 0)
  {
    std::string file(this->FileName);
    gmsh::initialize();
    gmsh::option::setNumber("General.Verbosity", 1);
    gmsh::option::setNumber("PostProcessing.SaveMesh", 0);
    gmsh::model::add(this->Internal->ModelName.c_str());
    this->SetUpEntities();
    if (!this->SetUpPhysicalGroups())
    {
      return 0;
    }

    this->Internal->Dimension = 3;
    // Get tag of the current model
    {
      gmsh::vectorpair tmp;
      gmsh::model::getEntities(tmp);
      this->Internal->ModelTag = tmp[0].second;
    }

    // Load and save topology
    this->LoadNodes();
    this->LoadCells();
    gmsh::write(this->FileName);

    this->InitViews();
    if (this->WriteAllTimeSteps)
    {
      request->Set(vtkStreamingDemandDrivenPipeline::CONTINUE_EXECUTING(), 1);
    }
  }

  this->LoadNodeData();
  this->LoadCellData();
  this->Internal->CurrentTimeStep++;

  const int localContinue = request->Get(vtkStreamingDemandDrivenPipeline::CONTINUE_EXECUTING());
  if (this->Internal->CurrentTimeStep >= this->Internal->NumberOfTimeSteps || !localContinue)
  {
    // Tell the pipeline to stop looping.
    request->Set(vtkStreamingDemandDrivenPipeline::CONTINUE_EXECUTING(), 0);
    this->WriteData();
    gmsh::clear();
    gmsh::finalize();
  }

  return 1;
}

//------------------------------------------------------------------------------
void vtkGmshWriter::WriteData()
{
  const int numViews = this->Internal->CellViews.size() + this->Internal->NodeViews.size();
  for (int tag = 0; tag < numViews; ++tag)
  {
    gmsh::view::write(tag, this->FileName, true);
  }
}

//------------------------------------------------------------------------------
vtkUnstructuredGrid* vtkGmshWriter::GetInput()
{
  return vtkUnstructuredGrid::SafeDownCast(this->Superclass::GetInput());
}

//------------------------------------------------------------------------------
vtkUnstructuredGrid* vtkGmshWriter::GetInput(int port)
{
  return vtkUnstructuredGrid::SafeDownCast(this->Superclass::GetInput(port));
}

//-----------------------------------------------------------------------------
int vtkGmshWriter::FillInputPortInformation(int, vtkInformation* info)
{
  info->Remove(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE());
  info->Append(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkUnstructuredGrid");
  return 1;
}

//-----------------------------------------------------------------------------
void vtkGmshWriter::PrintSelf(std::ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "FileName: " << (this->GetFileName() ? this->GetFileName() : "(none)") << indent
     << ", WriteAllTimeSteps: " << this->WriteAllTimeSteps << indent
     << ", WriteGmshSpecificArray: " << this->WriteGmshSpecificArray << std::endl;
}
VTK_ABI_NAMESPACE_END
