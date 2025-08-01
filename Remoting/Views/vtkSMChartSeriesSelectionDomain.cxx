// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause
#include "vtkSMChartSeriesSelectionDomain.h"

#include "vtkChartRepresentation.h"
#include "vtkCommand.h"
#include "vtkDataObject.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPVArrayInformation.h"
#include "vtkPVDataInformation.h"
#include "vtkPVDataSetAttributesInformation.h"
#include "vtkPVGeneralSettings.h"
#include "vtkPVRepresentedArrayListSettings.h"
#include "vtkPVXMLElement.h"
#include "vtkSMArrayListDomain.h"
#include "vtkSMIntVectorProperty.h"
#include "vtkSMProperty.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMStringVectorProperty.h"
#include "vtkSMTransferFunctionPresets.h"
#include "vtkSMTransferFunctionProxy.h"
#include "vtkSMUncheckedPropertyHelper.h"
#include "vtkStringList.h"

#include <vtk_jsoncpp.h>
#include <vtksys/RegularExpression.hxx>

#include <cassert>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <vector>

vtkStandardNewMacro(vtkSMChartSeriesSelectionDomain);
namespace
{
// match string like: "ACCL (0)" or "VEL (1)"
static vtksys::RegularExpression PotentialComponentNameRe(".*\\([0-9]+\\)");
}

class vtkSMChartSeriesSelectionDomain::vtkInternals
{
  unsigned int ColorCounter;

public:
  std::map<std::string, bool> VisibilityOverrides;

  vtkInternals()
    : ColorCounter(0)
  {
  }

  void GetNextColor(const char* presetName, double rgb[3])
  {
    auto presets = vtkSMTransferFunctionPresets::GetInstance();
    const char* name = presetName ? presetName : "";
    if (!presets->HasPreset(name))
    {
      vtkWarningWithObjectMacro(
        nullptr, "No preset with this name (" << name << "). Fall back to Spectrum.");
      name = "Spectrum";
    }
    const Json::Value& preset = presets->GetFirstPresetWithName(name);
    const Json::Value& indexedColors = preset["IndexedColors"];
    auto nbOfColors = indexedColors.size() / 3;
    int idx = this->ColorCounter % nbOfColors;
    rgb[0] = indexedColors[3 * idx].asDouble();
    rgb[1] = indexedColors[3 * idx + 1].asDouble();
    rgb[2] = indexedColors[3 * idx + 2].asDouble();
    this->ColorCounter++;
  }
};

//----------------------------------------------------------------------------
vtkSMChartSeriesSelectionDomain::vtkSMChartSeriesSelectionDomain()
  : Internals(new vtkSMChartSeriesSelectionDomain::vtkInternals())
{
  this->DefaultMode = vtkSMChartSeriesSelectionDomain::UNDEFINED;
  this->DefaultValue = nullptr;
  this->SetDefaultValue("");
  this->FlattenTable = true;
  this->HidePartialArrays = true;

  this->AddObserver(
    vtkCommand::DomainModifiedEvent, this, &vtkSMChartSeriesSelectionDomain::OnDomainModified);
}

//----------------------------------------------------------------------------
vtkSMChartSeriesSelectionDomain::~vtkSMChartSeriesSelectionDomain()
{
  this->SetDefaultValue(nullptr);
  delete this->Internals;
  this->Internals = nullptr;
}

//----------------------------------------------------------------------------
vtkPVDataInformation* vtkSMChartSeriesSelectionDomain::GetInputInformation()
{
  vtkSMProperty* inputProperty = this->GetRequiredProperty("Input");
  assert(inputProperty);

  vtkSMUncheckedPropertyHelper helper(inputProperty);
  if (helper.GetNumberOfElements() > 0)
  {
    vtkSMSourceProxy* sp = vtkSMSourceProxy::SafeDownCast(helper.GetAsProxy(0));
    if (sp)
    {
      return sp->GetDataInformation(helper.GetOutputPort());
    }
  }
  return nullptr;
}

//----------------------------------------------------------------------------
int vtkSMChartSeriesSelectionDomain::ReadXMLAttributes(
  vtkSMProperty* prop, vtkPVXMLElement* element)
{
  if (!this->Superclass::ReadXMLAttributes(prop, element))
  {
    return 0;
  }

  const char* default_mode = element->GetAttribute("default_mode");
  if (default_mode)
  {
    if (strcmp(default_mode, "visibility") == 0)
    {
      this->DefaultMode = vtkSMChartSeriesSelectionDomain::VISIBILITY;
    }
    else if (strcmp(default_mode, "label") == 0)
    {
      this->DefaultMode = vtkSMChartSeriesSelectionDomain::LABEL;
    }
    else if (strcmp(default_mode, "color") == 0)
    {
      this->DefaultMode = vtkSMChartSeriesSelectionDomain::COLOR;
    }
    else if (strcmp(default_mode, "value") == 0)
    {
      this->DefaultMode = vtkSMChartSeriesSelectionDomain::VALUE;
      if (element->GetAttribute("default_value"))
      {
        this->SetDefaultValue(element->GetAttribute("default_value"));
      }
    }
  }

  const char* flatten_table = element->GetAttribute("flatten_table");
  if (flatten_table)
  {
    this->FlattenTable = (strcmp(default_mode, "true") == 0);
  }
  int hide_partial_arrays;
  if (element->GetScalarAttribute("hide_partial_arrays", &hide_partial_arrays))
  {
    this->HidePartialArrays = (hide_partial_arrays == 1);
  }
  return 1;
}

//----------------------------------------------------------------------------
void vtkSMChartSeriesSelectionDomain::Update(vtkSMProperty*)
{
  vtkSMProperty* input = this->GetRequiredProperty("Input");
  if (!input)
  {
    vtkErrorMacro("Missing require property 'Input'. Update failed.");
    return;
  }
  vtkPVDataInformation* dataInfo = this->GetInputDataInformation("Input");
  if (!dataInfo)
  {
    return;
  }
  const auto fieldDataSelection =
    vtkSMIntVectorProperty::SafeDownCast(this->GetRequiredProperty("FieldDataSelection"));
  if (!fieldDataSelection)
  {
    vtkErrorMacro("Missing required property 'FieldDataSelection'. Update failed.");
    return;
  }
  const auto activeAssembly =
    vtkSMStringVectorProperty::SafeDownCast(this->GetRequiredProperty("ActiveAssembly"));
  const auto selectors =
    vtkSMStringVectorProperty::SafeDownCast(this->GetRequiredProperty("Selectors"));

  // clear old component names.
  this->Internals->VisibilityOverrides.clear();

  if (!activeAssembly || !selectors || !dataInfo->IsCompositeDataSet())
  {
    // since there's no way to choose which dataset from a composite one to use,
    // just look at the top-level array information (skipping partial arrays).
    std::vector<std::string> column_names;
    const int fieldAssociation = fieldDataSelection->GetUncheckedElement(0);
    this->PopulateAvailableArrays(
      std::string(), column_names, dataInfo, fieldAssociation, this->FlattenTable);
    this->SetStrings(column_names);
    return;
  }

  assert(activeAssembly->GetNumberOfUncheckedElements() == 1);

  std::vector<std::string> column_names;
  const int fieldAssociation = fieldDataSelection->GetUncheckedElement(0);
  const unsigned int numElems = selectors->GetNumberOfUncheckedElements();
  for (unsigned int cc = 0; cc < numElems; cc++)
  {
    std::string blockName;
    if (selectors->GetRepeatCommand())
    {
      const auto blockNames = dataInfo->GetBlockNames(
        { selectors->GetUncheckedElement(cc) }, activeAssembly->GetUncheckedElement(0));
      blockName = blockNames.empty() ? selectors->GetUncheckedElement(cc) : blockNames.front();
    }

    auto childInfo = this->GetInputSubsetDataInformation(
      selectors->GetUncheckedElement(cc), activeAssembly->GetUncheckedElement(0), "Input");
    this->PopulateAvailableArrays(
      blockName, column_names, childInfo, fieldAssociation, this->FlattenTable);
  }
  this->SetStrings(column_names);
}

//----------------------------------------------------------------------------
// Add arrays from dataInfo to strings. If blockName is non-empty, then it's
// used to "uniquify" the array names.
void vtkSMChartSeriesSelectionDomain::PopulateAvailableArrays(const std::string& blockName,
  std::vector<std::string>& strings, vtkPVDataInformation* dataInfo, int fieldAssociation,
  bool flattenTable)
{
  // this method is typically called for leaf nodes (or multi-piece).
  // assert((dataInfo->GetCompositeDataInformation()->GetDataIsComposite() == 0) ||
  //   (dataInfo->GetCompositeDataInformation()->GetDataIsMultiPiece() == 0));
  vtkChartRepresentation* chartRepr =
    vtkChartRepresentation::SafeDownCast(this->GetProperty()->GetParent()->GetClientSideObject());
  if (!chartRepr || !dataInfo)
  {
    return;
  }

  // helps use avoid duplicates. duplicates may arise for plot types that treat
  // multiple columns as a single series/plot e.g. quartile plots.
  std::set<std::string> uniquestrings;

  vtkPVDataSetAttributesInformation* dsa = dataInfo->GetAttributeInformation(fieldAssociation);
  if (dsa != nullptr)
  {
    std::unique_ptr<vtkPVDataSetAttributesInformation::AlphabeticalArrayInformationIterator> iter(
      dsa->NewAlphabeticalArrayInformationIterator());
    for (iter->GoToFirstItem(); !iter->IsDoneWithTraversal(); iter->GoToNextItem())
    {
      vtkPVArrayInformation* arrayInfo = iter->GetCurrentArrayInformation();
      this->PopulateArrayComponents(
        chartRepr, blockName, strings, uniquestrings, arrayInfo, flattenTable);
    }
  }

  if (fieldAssociation == vtkDataObject::FIELD_ASSOCIATION_POINTS)
  {
    vtkPVArrayInformation* pointArrayInfo = dataInfo->GetPointArrayInformation();
    this->PopulateArrayComponents(
      chartRepr, blockName, strings, uniquestrings, pointArrayInfo, flattenTable);
  }
}

//----------------------------------------------------------------------------
// Add array component from arrayInfo to strings. If blockName is non-empty, then it's
// used to "uniquify" the array names.
void vtkSMChartSeriesSelectionDomain::PopulateArrayComponents(vtkChartRepresentation* chartRepr,
  const std::string& blockName, std::vector<std::string>& strings,
  std::set<std::string>& uniquestrings, vtkPVArrayInformation* arrayInfo, bool flattenTable)
{
  if (arrayInfo && (!this->HidePartialArrays || arrayInfo->GetIsPartial() == 0))
  {
    int dataType = arrayInfo->GetDataType();
    if (dataType != VTK_STRING && dataType != VTK_VARIANT)
    {
      if (arrayInfo->GetNumberOfComponents() > 1 && flattenTable)
      {
        for (int kk = 0; kk <= arrayInfo->GetNumberOfComponents(); kk++)
        {
          std::string component_name = vtkSMArrayListDomain::CreateMangledName(arrayInfo, kk);
          component_name = chartRepr->GetDefaultSeriesLabel(blockName, component_name);
          if (uniquestrings.find(component_name) == uniquestrings.end())
          {
            strings.push_back(component_name);
            uniquestrings.insert(component_name);
          }
          if (kk != arrayInfo->GetNumberOfComponents())
          {
            // save component names so we can detect them when setting defaults
            // later.
            this->SetDefaultVisibilityOverride(component_name, false);
          }
        }
      }
      else
      {
        const char* arrayName = arrayInfo->GetName();
        if (arrayName != nullptr)
        {
          std::string seriesName = chartRepr->GetDefaultSeriesLabel(blockName, arrayName);
          if (uniquestrings.find(seriesName) == uniquestrings.end())
          {
            strings.push_back(seriesName);
            uniquestrings.insert(seriesName);

            // Special case for Quartile plots. PlotSelectionOverTime filter, when
            // produces stats, likes to pre-split components in an array into multiple
            // single component arrays. We still want those to be treated as
            // components and not be shown by default. Hence, we use this hack.
            // (See BUG #15512).
            std::string seriesNameWithoutTableName =
              chartRepr->GetDefaultSeriesLabel(std::string(), arrayName);
            if (PotentialComponentNameRe.find(seriesNameWithoutTableName))
            {
              this->SetDefaultVisibilityOverride(seriesName, false);
            }
          }
        }
      }
    }
  }
}

//----------------------------------------------------------------------------
std::vector<std::string> vtkSMChartSeriesSelectionDomain::GetDefaultValue(const char* series)
{
  std::vector<std::string> values;
  if (this->DefaultMode == VISIBILITY)
  {
    values.push_back(this->GetDefaultSeriesVisibility(series) ? "1" : "0");
  }
  else if (this->DefaultMode == LABEL)
  {
    // by default, label is same as the name of the series.
    values.push_back(series);
  }
  else if (this->DefaultMode == COLOR)
  {
    double rgb[3];
    auto presetProp = this->GetProperty()->GetParent()->GetProperty("LastPresetName");
    this->Internals->GetNextColor(
      presetProp ? vtkSMPropertyHelper(presetProp).GetAsString() : "Spectrum", rgb);
    for (int kk = 0; kk < 3; kk++)
    {
      std::ostringstream stream;
      stream << std::setprecision(2) << rgb[kk];
      values.push_back(stream.str());
    }
  }
  else if (this->DefaultMode == VALUE)
  {
    values.push_back(this->DefaultValue);
  }
  return values;
}

//----------------------------------------------------------------------------
int vtkSMChartSeriesSelectionDomain::SetDefaultValues(
  vtkSMProperty* property, bool use_unchecked_values)
{
  vtkSMStringVectorProperty* vp = vtkSMStringVectorProperty::SafeDownCast(property);
  if (!vp)
  {
    vtkErrorMacro("Property must be a vtkSMVectorProperty subclass.");
    return 0;
  }
  if (use_unchecked_values)
  {
    vtkWarningMacro("Developer warning: use_unchecked_values not implemented yet.");
  }

  this->UpdateDefaultValues(property, false);
  return 1;
}

//----------------------------------------------------------------------------
void vtkSMChartSeriesSelectionDomain::UpdateDefaultValues(
  vtkSMProperty* property, bool preserve_previous_values)
{
  vtkSMStringVectorProperty* vp = vtkSMStringVectorProperty::SafeDownCast(property);
  assert(vp != nullptr);

  vtkNew<vtkStringList> values;
  std::set<std::string> seriesNames;
  if (preserve_previous_values)
  {
    // capture old values.
    unsigned int numElems = vp->GetNumberOfElements();
    int stepSize =
      vp->GetNumberOfElementsPerCommand() > 0 ? vp->GetNumberOfElementsPerCommand() : 1;

    for (unsigned int cc = 0; (cc + stepSize) <= numElems; cc += stepSize)
    {
      seriesNames.insert(vp->GetElement(cc) ? vp->GetElement(cc) : "");
      for (int kk = 0; kk < stepSize; kk++)
      {
        values->AddString(vp->GetElement(cc + kk));
      }
    }
  }

  const std::vector<std::string>& domain_strings = this->GetStrings();
  for (size_t cc = 0; cc < domain_strings.size(); cc++)
  {
    if (preserve_previous_values && seriesNames.find(domain_strings[cc]) != seriesNames.end())
    {
      // skip this. This series had a value set already which we are requested
      // to preserve.
      continue;
    }
    std::vector<std::string> cur_values = this->GetDefaultValue(domain_strings[cc].c_str());
    if (!cur_values.empty())
    {
      values->AddString(domain_strings[cc].c_str());
      for (size_t kk = 0; kk < cur_values.size(); kk++)
      {
        values->AddString(cur_values[kk].c_str());
      }
    }
  }

  vp->SetElements(values.GetPointer());
}

//----------------------------------------------------------------------------
bool vtkSMChartSeriesSelectionDomain::GetDefaultSeriesVisibility(const char* name)
{
  if (vtkPVGeneralSettings::GetInstance()->GetLoadNoChartVariables())
  {
    return false;
  }
  if (!vtkPVRepresentedArrayListSettings::GetInstance()->GetSeriesVisibilityDefault(name))
  {
    return false;
  }
  if (this->Internals->VisibilityOverrides.find(name) != this->Internals->VisibilityOverrides.end())
  {
    //  hide components by default, we'll show the magnitudes for them.
    return this->Internals->VisibilityOverrides[name];
  }

  return true;
}

//----------------------------------------------------------------------------
void vtkSMChartSeriesSelectionDomain::OnDomainModified()
{
  vtkSMProperty* prop = this->GetProperty();
  this->UpdateDefaultValues(prop, true);
  if (prop->GetParent())
  {
    prop->GetParent()->UpdateProperty(prop->GetXMLName());
  }
}

//----------------------------------------------------------------------------
void vtkSMChartSeriesSelectionDomain::SetDefaultVisibilityOverride(
  const std::string& arrayname, bool visibility)
{
  this->Internals->VisibilityOverrides[arrayname] = visibility;
}

//----------------------------------------------------------------------------
void vtkSMChartSeriesSelectionDomain::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "HidePartialArrays: " << this->HidePartialArrays << endl;
}

//----------------------------------------------------------------------------
void vtkSMChartSeriesSelectionDomain::SetLoadNoChartVariables(bool choice)
{
  vtkPVGeneralSettings::GetInstance()->SetLoadNoChartVariables(choice);
}

//----------------------------------------------------------------------------
bool vtkSMChartSeriesSelectionDomain::GetLoadNoChartVariables()
{
  return vtkPVGeneralSettings::GetInstance()->GetLoadNoChartVariables();
}
