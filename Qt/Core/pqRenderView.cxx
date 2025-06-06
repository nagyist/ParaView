// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-FileCopyrightText: Copyright (c) Sandia Corporation
// SPDX-License-Identifier: BSD-3-Clause
#include "pqRenderView.h"

// ParaView Server Manager includes.
#include "vtkCollection.h"
#include "vtkIntArray.h"
#include "vtkLogger.h"
#include "vtkPVDataInformation.h"
#include "vtkPVRenderView.h"
#include "vtkPVRenderViewSettings.h"
#include "vtkPVXMLElement.h"
#include "vtkProcessModule.h"
#include "vtkSMCameraLink.h"
#include "vtkSMIntVectorProperty.h"
#include "vtkSMInteractionUndoStackBuilder.h"
#include "vtkSMLink.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxyManager.h"
#include "vtkSMProxyProperty.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMRepresentationProxy.h"
#include "vtkSMSelectionHelper.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMTrace.h"
#include "vtkSMUndoStack.h"
#include "vtkSelection.h"
#include "vtkSelectionNode.h"
#include "vtkSmartPointer.h"

// Qt includes.
#include <QEvent>
#include <QGridLayout>
#include <QList>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QtDebug>

// ParaView includes.
#include "pqApplicationCore.h"
#include "pqDataRepresentation.h"
#include "pqLinkViewWidget.h"
#include "pqLinksModel.h"
#include "pqOutputPort.h"
#include "pqPipelineSource.h"
#include "pqQVTKWidget.h"
#include "pqSMAdaptor.h"
#include "pqServer.h"
#include "pqServerManagerModel.h"
#include "pqUndoStack.h"

#include <algorithm>

namespace
{

// converts a vtkSMRepresentationProxy to its corresponding pqDataRepresentation
pqDataRepresentation* findRepresentationFromProxy(vtkSMRepresentationProxy* proxy)
{
  if (!proxy)
  {
    return nullptr;
  }

  pqServerManagerModel* smm = pqApplicationCore::instance()->getServerManagerModel();

  return smm->findItem<pqDataRepresentation*>(proxy);
}

} // end anonymous namespace

class pqRenderView::pqInternal
{
public:
  vtkSmartPointer<vtkSMUndoStack> InteractionUndoStack;
  vtkSmartPointer<vtkSMInteractionUndoStackBuilder> UndoStackBuilder;
  QList<pqRenderView*> LinkedUndoStacks;
  bool UpdatingStack;
  int CurrentInteractionMode;

  pqInternal()
  {
    this->CurrentInteractionMode = -1;
    this->UpdatingStack = false;
    this->InteractionUndoStack = vtkSmartPointer<vtkSMUndoStack>::New();
    // FIXME this->InteractionUndoStack->SetClientOnly(true);
    this->UndoStackBuilder = vtkSmartPointer<vtkSMInteractionUndoStackBuilder>::New();
    this->UndoStackBuilder->SetUndoStack(this->InteractionUndoStack);
  }

  ~pqInternal() = default;

  // Convenience method to update the view widget cursor
  void UpdateCursor(QWidget* widget)
  {
    QCursor cursor = this->CursorVisible ? this->Cursor : QCursor(Qt::BlankCursor);
    if (auto* qvtkWidget = qobject_cast<pqQVTKWidget*>(widget))
    {
      qvtkWidget->setCursorCustom(cursor);
    }
    else
    {
      widget->setCursor(cursor);
    }
  }

  QCursor Cursor;
  bool CursorVisible = true;
};

namespace
{

std::string GetSelectionModifierAsString(int selectionModifier)
{
  std::string modifier;
  if (selectionModifier == pqView::PV_SELECTION_ADDITION)
  {
    modifier = "ADD";
  }
  else if (selectionModifier == pqView::PV_SELECTION_SUBTRACTION)
  {
    modifier = "SUBTRACT";
  }
  else if (selectionModifier == pqView::PV_SELECTION_TOGGLE)
  {
    modifier = "TOGGLE";
  }

  return modifier;
}

} // end anonymous namespace

//-----------------------------------------------------------------------------
void pqRenderView::InternalConstructor(vtkSMViewProxy* renModule)
{
  this->Internal = new pqRenderView::pqInternal();

  // we need to fire signals when undo stack changes.
  this->getConnector()->Connect(this->Internal->InteractionUndoStack, vtkCommand::ModifiedEvent,
    this, SLOT(onUndoStackChanged()), nullptr, 0, Qt::QueuedConnection);

  this->ResetCenterWithCamera = true;
  this->UseMultipleRepresentationSelection = false;
  this->getConnector()->Connect(
    renModule, vtkCommand::ResetCameraEvent, this, SLOT(onResetCameraEvent()));

  // Monitor any interaction mode change
  this->getConnector()->Connect(this->getProxy()->GetProperty("InteractionMode"),
    vtkCommand::ModifiedEvent, this, SLOT(onInteractionModeChange()));

  // Reuse tone mapping parameters set by the user if no presets are selected
  this->getConnector()->Connect(this->getProxy()->GetProperty("GenericFilmicPresets"),
    vtkCommand::ModifiedEvent, this, SLOT(onGenericFilmicPresetsChange()));
}

//-----------------------------------------------------------------------------
pqRenderView::pqRenderView(const QString& group, const QString& name, vtkSMViewProxy* renModule,
  pqServer* server, QObject* _parent /*=null*/)
  : Superclass(renderViewType(), group, name, renModule, server, _parent)
{
  this->InternalConstructor(renModule);
}

//-----------------------------------------------------------------------------
pqRenderView::pqRenderView(const QString& type, const QString& group, const QString& name,
  vtkSMViewProxy* renModule, pqServer* server, QObject* _parent /*=null*/)
  : Superclass(type, group, name, renModule, server, _parent)
{
  this->InternalConstructor(renModule);
}

//-----------------------------------------------------------------------------
pqRenderView::~pqRenderView()
{
  delete this->Internal;
}

//-----------------------------------------------------------------------------
vtkSMRenderViewProxy* pqRenderView::getRenderViewProxy() const
{
  return vtkSMRenderViewProxy::SafeDownCast(this->getViewProxy());
}

//-----------------------------------------------------------------------------
void pqRenderView::initialize()
{
  this->Superclass::initialize();

  // initialize the interaction undo-redo stack.
  vtkSMRenderViewProxy* viewProxy = this->getRenderViewProxy();
  this->Internal->UndoStackBuilder->SetRenderView(viewProxy);

  if (this->getProxy()->GetHints() &&
    this->getProxy()->GetHints()->FindNestedElementByName("HideCursor"))
  {
    this->setCursorVisible(false);
  }
}

//-----------------------------------------------------------------------------
QWidget* pqRenderView::createWidget()
{
  QWidget* vtkwidget = this->Superclass::createWidget();
  if (pqQVTKWidget* qvtkwidget = qobject_cast<pqQVTKWidget*>(vtkwidget))
  {
    vtkSMRenderViewProxy* renModule = this->getRenderViewProxy();
    qvtkwidget->setRenderWindow(renModule->GetRenderWindow());
    // This is needed to ensure that the interactor is initialized with
    // ParaView specific interactor styles etc.
    renModule->SetupInteractor(qvtkwidget->interactor());
  }
  return vtkwidget;
}

//-----------------------------------------------------------------------------
void pqRenderView::onResetCameraEvent()
{
  if (this->ResetCenterWithCamera)
  {
    this->resetParallelScale();
    this->resetCenterOfRotation();
  }
}

//-----------------------------------------------------------------------------
void pqRenderView::resetCamera(bool closest, double offsetRatio)
{
  this->fakeInteraction(true);
  this->getRenderViewProxy()->ResetCamera(closest, offsetRatio);
  this->fakeInteraction(false);
  this->render();
}

//-----------------------------------------------------------------------------
void pqRenderView::resetCenterOfRotation()
{
  // Update center of rotation.
  vtkSMProxy* viewproxy = this->getProxy();
  viewproxy->UpdatePropertyInformation();
  QList<QVariant> values =
    pqSMAdaptor::getMultipleElementProperty(viewproxy->GetProperty("CameraFocalPointInfo"));
  this->setCenterOfRotation(values[0].toDouble(), values[1].toDouble(), values[2].toDouble());
}

//-----------------------------------------------------------------------------
void pqRenderView::resetParallelScale()
{
  // Update parallel scale.
  vtkSMProxy* viewproxy = this->getProxy();
  viewproxy->UpdatePropertyInformation();
  QVariant parallelScaleInfo =
    pqSMAdaptor::getElementProperty(viewproxy->GetProperty("CameraParallelScaleInfo"));
  this->setParallelScale(parallelScaleInfo.toDouble());
}

//-----------------------------------------------------------------------------
void pqRenderView::setOrientationAxesVisibility(bool visible)
{
  vtkSMPropertyHelper(this->getProxy(), "OrientationAxesVisibility", true).Set(visible ? 1 : 0);
  this->getProxy()->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
bool pqRenderView::getOrientationAxesVisibility() const
{
  return vtkSMPropertyHelper(this->getProxy(), "OrientationAxesVisibility", true).GetAsInt() == 0
    ? false
    : true;
}

//-----------------------------------------------------------------------------
void pqRenderView::setOrientationAxesInteractivity(bool interactive)
{
  vtkSMPropertyHelper(this->getProxy(), "OrientationAxesInteractivity").Set(interactive ? 1 : 0);
  this->getProxy()->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
bool pqRenderView::getOrientationAxesInteractivity() const
{
  return vtkSMPropertyHelper(this->getProxy(), "OrientationAxesInteractivity").GetAsInt() == 0
    ? false
    : true;
}

//-----------------------------------------------------------------------------
void pqRenderView::setOrientationAxesOutlineColor(const QColor& color)
{
  double colorf[3];
  colorf[0] = color.redF();
  colorf[1] = color.greenF();
  colorf[2] = color.blueF();
  vtkSMPropertyHelper(this->getProxy(), "OrientationAxesOutlineColor").Set(colorf, 3);
  this->getProxy()->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
QColor pqRenderView::getOrientationAxesOutlineColor() const
{
  QColor color;
  double dcolor[3];
  vtkSMPropertyHelper(this->getProxy(), "OrientationAxesOutlineColor").Get(dcolor, 3);
  color.setRgbF(dcolor[0], dcolor[1], dcolor[2]);
  return color;
}

//-----------------------------------------------------------------------------
void pqRenderView::setOrientationAxesLabelColor(const QColor& color)
{
  double colorf[3];
  colorf[0] = color.redF();
  colorf[1] = color.greenF();
  colorf[2] = color.blueF();
  vtkSMPropertyHelper(this->getProxy(), "OrientationAxesLabelColor", true).Set(colorf, 3);
  this->getProxy()->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
QColor pqRenderView::getOrientationAxesLabelColor() const
{
  QColor color;
  double dcolor[3] = { 1, 1, 1 };
  vtkSMPropertyHelper(this->getProxy(), "OrientationAxesLabelColor", true).Get(dcolor, 3);
  color.setRgbF(dcolor[0], dcolor[1], dcolor[2]);
  return color;
}

//-----------------------------------------------------------------------------
void pqRenderView::setCenterOfRotation(double x, double y, double z)
{
  QList<QVariant> positionValues;
  positionValues << x << y << z;
  vtkSMProxy* viewproxy = this->getProxy();
  pqSMAdaptor::setMultipleElementProperty(
    viewproxy->GetProperty("CenterOfRotation"), positionValues);
  viewproxy->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
void pqRenderView::setParallelScale(double scale)
{
  vtkSMProxy* viewproxy = this->getProxy();
  pqSMAdaptor::setElementProperty(viewproxy->GetProperty("CameraParallelScale"), scale);
  viewproxy->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
void pqRenderView::getCenterOfRotation(double center[3]) const
{
  QList<QVariant> val =
    pqSMAdaptor::getMultipleElementProperty(this->getProxy()->GetProperty("CenterOfRotation"));
  center[0] = val[0].toDouble();
  center[1] = val[1].toDouble();
  center[2] = val[2].toDouble();
}

//-----------------------------------------------------------------------------
void pqRenderView::setCenterAxesVisibility(bool visible)
{
  pqSMAdaptor::setElementProperty(
    this->getProxy()->GetProperty("CenterAxesVisibility"), visible ? 1 : 0);
  this->getProxy()->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
bool pqRenderView::getCenterAxesVisibility() const
{
  return pqSMAdaptor::getElementProperty(this->getProxy()->GetProperty("CenterAxesVisibility"))
    .toBool();
}

//-----------------------------------------------------------------------------
void pqRenderView::linkToOtherView()
{
  pqLinkViewWidget* linkWidget = new pqLinkViewWidget(this);
  linkWidget->setAttribute(Qt::WA_DeleteOnClose);
  QPoint pos = this->widget()->mapToGlobal(QPoint(2, 2));
  linkWidget->move(pos);
  linkWidget->show();
}

//-----------------------------------------------------------------------------
void pqRenderView::removeViewLinks()
{
  pqLinksModel* model = pqApplicationCore::instance()->getLinksModel();
  vtkNew<vtkCollection> cameraLinks;
  vtkSMSessionProxyManager* pxm = this->proxyManager();
  model->FindLinksFromProxy(this->getViewProxy(), vtkSMLink::NONE, cameraLinks.Get());

  for (int idx = 0; idx < cameraLinks->GetNumberOfItems(); idx++)
  {
    vtkSMCameraLink* cameraLink = vtkSMCameraLink::SafeDownCast(cameraLinks->GetItemAsObject(idx));
    if (cameraLink != nullptr)
    {
      const char* linkName = pxm->GetRegisteredLinkName(cameraLink);
      model->removeLink(linkName);
    }
  }
}

//-----------------------------------------------------------------------------
void pqRenderView::onUndoStackChanged()
{
  bool can_undo = this->Internal->InteractionUndoStack->CanUndo();
  bool can_redo = this->Internal->InteractionUndoStack->CanRedo();

  Q_EMIT this->canUndoChanged(can_undo);
  Q_EMIT this->canRedoChanged(can_redo);
}

//-----------------------------------------------------------------------------
bool pqRenderView::canUndo() const
{
  return this->Internal->InteractionUndoStack->CanUndo();
}

//-----------------------------------------------------------------------------
bool pqRenderView::canRedo() const
{
  return this->Internal->InteractionUndoStack->CanRedo();
}

//-----------------------------------------------------------------------------
void pqRenderView::undo()
{
  this->Internal->InteractionUndoStack->Undo();
  this->getProxy()->UpdateVTKObjects();
  this->render();

  this->fakeUndoRedo(false, false);
}

//-----------------------------------------------------------------------------
void pqRenderView::redo()
{
  this->Internal->InteractionUndoStack->Redo();
  this->getProxy()->UpdateVTKObjects();
  this->render();

  this->fakeUndoRedo(true, false);
}

//-----------------------------------------------------------------------------
void pqRenderView::linkUndoStack(pqRenderView* other)
{
  if (other == this)
  {
    // Sanity check, nothing to link if both are same.
    return;
  }

  this->Internal->LinkedUndoStacks.push_back(other);

  // Clear all linked stacks until now.
  this->clearUndoStack();
}

//-----------------------------------------------------------------------------
void pqRenderView::unlinkUndoStack(pqRenderView* other)
{
  if (!other || other == this)
  {
    return;
  }
  this->Internal->LinkedUndoStacks.removeAll(other);
}

//-----------------------------------------------------------------------------
void pqRenderView::clearUndoStack()
{
  if (this->Internal->UpdatingStack)
  {
    return;
  }
  this->Internal->UpdatingStack = true;
  this->Internal->InteractionUndoStack->Clear();
  Q_FOREACH (pqRenderView* other, this->Internal->LinkedUndoStacks)
  {
    if (other)
    {
      other->clearUndoStack();
    }
  }
  this->Internal->UpdatingStack = false;
}

//-----------------------------------------------------------------------------
void pqRenderView::fakeUndoRedo(bool fake_redo, bool self)
{
  if (this->Internal->UpdatingStack)
  {
    return;
  }
  this->Internal->UpdatingStack = true;
  if (self)
  {
    if (fake_redo)
    {
      this->Internal->InteractionUndoStack->PopRedoStack();
    }
    else
    {
      this->Internal->InteractionUndoStack->PopUndoStack();
    }
  }
  Q_FOREACH (pqRenderView* other, this->Internal->LinkedUndoStacks)
  {
    if (other)
    {
      other->fakeUndoRedo(fake_redo, true);
    }
  }
  this->Internal->UpdatingStack = false;
}

//-----------------------------------------------------------------------------
void pqRenderView::fakeInteraction(bool start)
{
  if (this->Internal->UpdatingStack)
  {
    return;
  }

  this->Internal->UpdatingStack = true;

  if (start)
  {
    this->Internal->UndoStackBuilder->StartInteraction();
  }
  else
  {
    this->Internal->UndoStackBuilder->EndInteraction();
  }

  Q_FOREACH (pqRenderView* other, this->Internal->LinkedUndoStacks)
  {
    other->fakeInteraction(start);
  }
  this->Internal->UpdatingStack = false;
}

//-----------------------------------------------------------------------------
void pqRenderView::resetViewDirection(
  double look_x, double look_y, double look_z, double up_x, double up_y, double up_z)
{
  vtkSMRenderViewProxy* const proxy = this->getRenderViewProxy();
  proxy->ResetActiveCameraToDirection(look_x, look_y, look_z, up_x, up_y, up_z);
  this->resetCamera();
  this->render();
}

//-----------------------------------------------------------------------------
void pqRenderView::adjustView(const int& adjustType, const double& angle)
{
  vtkSMRenderViewProxy* const proxy = this->getRenderViewProxy();
  proxy->AdjustActiveCamera(adjustType, angle);
  this->render();
}

//-----------------------------------------------------------------------------
void pqRenderView::adjustAzimuth(const double& value)
{
  vtkSMRenderViewProxy* const proxy = this->getRenderViewProxy();
  proxy->AdjustAzimuth(value);
  this->render();
}

//-----------------------------------------------------------------------------
void pqRenderView::adjustElevation(const double& value)
{
  vtkSMRenderViewProxy* const proxy = this->getRenderViewProxy();
  proxy->AdjustElevation(value);
  this->render();
}

//-----------------------------------------------------------------------------
void pqRenderView::adjustRoll(const double& value)
{
  vtkSMRenderViewProxy* const proxy = this->getRenderViewProxy();
  proxy->AdjustRoll(value);
  this->render();
}

//-----------------------------------------------------------------------------
void pqRenderView::adjustZoom(const double& value)
{
  vtkSMRenderViewProxy* const proxy = this->getRenderViewProxy();
  proxy->AdjustZoom(value);
  this->render();
}

//-----------------------------------------------------------------------------
void pqRenderView::applyIsometricView()
{
  vtkSMRenderViewProxy* const proxy = this->getRenderViewProxy();
  proxy->ApplyIsometricView();
  this->resetCamera();
  this->render();
}

//-----------------------------------------------------------------------------
void pqRenderView::resetViewDirectionToPositiveX()
{
  vtkSMRenderViewProxy* const proxy = this->getRenderViewProxy();
  proxy->ResetActiveCameraToPositiveX();
  this->resetCamera();
  this->render();
}

//-----------------------------------------------------------------------------
void pqRenderView::resetViewDirectionToNegativeX()
{
  vtkSMRenderViewProxy* const proxy = this->getRenderViewProxy();
  proxy->ResetActiveCameraToNegativeX();
  this->resetCamera();
  this->render();
}

//-----------------------------------------------------------------------------
void pqRenderView::resetViewDirectionToPositiveY()
{
  vtkSMRenderViewProxy* const proxy = this->getRenderViewProxy();
  proxy->ResetActiveCameraToPositiveY();
  this->resetCamera();
  this->render();
}

//-----------------------------------------------------------------------------
void pqRenderView::resetViewDirectionToNegativeY()
{
  vtkSMRenderViewProxy* const proxy = this->getRenderViewProxy();
  proxy->ResetActiveCameraToNegativeY();
  this->resetCamera();
  this->render();
}

//-----------------------------------------------------------------------------
void pqRenderView::resetViewDirectionToPositiveZ()
{
  vtkSMRenderViewProxy* const proxy = this->getRenderViewProxy();
  proxy->ResetActiveCameraToPositiveZ();
  this->resetCamera();
  this->render();
}

//-----------------------------------------------------------------------------
void pqRenderView::resetViewDirectionToNegativeZ()
{
  vtkSMRenderViewProxy* const proxy = this->getRenderViewProxy();
  proxy->ResetActiveCameraToNegativeZ();
  this->resetCamera();
  this->render();
}

//-----------------------------------------------------------------------------
void pqRenderView::selectCellsOnSurface(int rect[4], int selectionModifier, const char* array)
{
  QList<pqOutputPort*> opPorts;
  this->selectOnSurfaceInternal(rect, opPorts, false, selectionModifier, false, array);
  this->emitSelectionSignal(opPorts, selectionModifier);
}

//-----------------------------------------------------------------------------
void pqRenderView::emitSelectionSignal(QList<pqOutputPort*> opPorts, int selectionModifier)
{
  // Fire selection event to let the world know that this view selected something.
  if (opPorts.count() > 0)
  {
    Q_EMIT this->selected(opPorts.value(0));
  }
  else
  {
    if (selectionModifier == pqView::PV_SELECTION_DEFAULT)
    {
      // Only emit an empty selection if we aren't modifying the current selection
      Q_EMIT this->selected(nullptr);
    }
  }

  if (this->UseMultipleRepresentationSelection)
  {
    Q_EMIT this->multipleSelected(opPorts);
  }
}

//-----------------------------------------------------------------------------
pqDataRepresentation* pqRenderView::pick(int pos[2])
{
  BEGIN_UNDO_EXCLUDE();
  vtkSMRenderViewProxy* renderView = this->getRenderViewProxy();
  vtkSMRepresentationProxy* repr = renderView->Pick(pos[0], pos[1]);
  pqDataRepresentation* pq_repr = findRepresentationFromProxy(repr);
  END_UNDO_EXCLUDE();
  if (pq_repr)
  {
    Q_EMIT this->picked(pq_repr->getOutputPortFromInput());
  }
  return pq_repr;
}

//-----------------------------------------------------------------------------
pqDataRepresentation* pqRenderView::pickBlock(int pos[2], unsigned int& flatIndex, int& rank)
{
  BEGIN_UNDO_EXCLUDE();
  vtkSMRenderViewProxy* renderView = this->getRenderViewProxy();
  vtkSMRepresentationProxy* repr = renderView->PickBlock(pos[0], pos[1], flatIndex, rank);
  pqDataRepresentation* pq_repr = findRepresentationFromProxy(repr);
  END_UNDO_EXCLUDE();
  if (pq_repr)
  {
    Q_EMIT this->picked(pq_repr->getOutputPortFromInput());
  }
  return pq_repr;
}

//-----------------------------------------------------------------------------
void pqRenderView::collectSelectionPorts(vtkCollection* selectedRepresentations,
  vtkCollection* selectionSources, QList<pqOutputPort*>& output_ports, int selectionModifier,
  bool select_blocks)
{
  if (!selectedRepresentations || selectedRepresentations->GetNumberOfItems() <= 0)
  {
    return;
  }

  if (!selectionSources || selectionSources->GetNumberOfItems() <= 0)
  {
    return;
  }

  if (selectedRepresentations->GetNumberOfItems() != selectionSources->GetNumberOfItems())
  {
    return;
  }

  for (int i = 0; i < selectedRepresentations->GetNumberOfItems(); i++)
  {
    vtkSMRepresentationProxy* selectedDataRepr =
      vtkSMRepresentationProxy::SafeDownCast(selectedRepresentations->GetItemAsObject(i));
    vtkSmartPointer<vtkSMSourceProxy> selectionSource =
      vtkSMSourceProxy::SafeDownCast(selectionSources->GetItemAsObject(i));

    pqServerManagerModel* smmodel = pqApplicationCore::instance()->getServerManagerModel();
    pqDataRepresentation* pqDataRepr = smmodel->findItem<pqDataRepresentation*>(selectedDataRepr);
    if (!selectedDataRepr)
    {
      // No data display was selected (or none that is registered).
      continue;
    }
    pqOutputPort* opPort = pqDataRepr->getOutputPortFromInput();
    vtkSMSourceProxy* prevAppendSelections = opPort->getSelectionInput();
    vtkSMSourceProxy* selectedDataSource =
      vtkSMSourceProxy::SafeDownCast(opPort->getSource()->getProxy());
    if (select_blocks)
    {
      // convert the index based selection to BLOCKS or BLOCK_SELECTORS.
      // Eventually, we want all datasets to simply create `BLOCK_SELECTORS`,
      // however I am worried this will impact custom applications. So we only
      // do this for vtkPartitionedDataSetCollection for now.
      auto dinfo = opPort->getDataInformation();
      const int targetContentType = dinfo->DataSetTypeIsA(VTK_PARTITIONED_DATA_SET_COLLECTION)
        ? vtkSelectionNode::BLOCK_SELECTORS
        : vtkSelectionNode::BLOCKS;
      selectionSource.TakeReference(
        vtkSMSourceProxy::SafeDownCast(vtkSMSelectionHelper::ConvertSelectionSource(
          targetContentType, selectionSource, selectedDataSource, opPort->getPortNumber())));
    }

    // create an append-selections proxy with the selection source as the only input
    vtkSmartPointer<vtkSMSourceProxy> newAppendSelections;
    newAppendSelections.TakeReference(vtkSMSourceProxy::SafeDownCast(
      vtkSMSelectionHelper::NewAppendSelectionsFromSelectionSource(selectionSource)));

    switch (selectionModifier)
    {
      case pqView::PV_SELECTION_ADDITION:
        vtkSMSelectionHelper::AddSelection(prevAppendSelections, newAppendSelections);
        break;
      case pqView::PV_SELECTION_SUBTRACTION:
        vtkSMSelectionHelper::SubtractSelection(prevAppendSelections, newAppendSelections);
        break;
      case pqView::PV_SELECTION_TOGGLE:
        vtkSMSelectionHelper::ToggleSelection(prevAppendSelections, newAppendSelections);
        break;
      case pqView::PV_SELECTION_DEFAULT:
      default:
        vtkSMSelectionHelper::IgnoreSelection(prevAppendSelections, newAppendSelections);
        break;
    }

    opPort->setSelectionInput(newAppendSelections, 0);
    output_ports.append(opPort);
  }
}

//-----------------------------------------------------------------------------
void pqRenderView::selectOnSurfaceInternal(int rect[4], QList<pqOutputPort*>& pqOutputPorts,
  bool select_points, int selectionModifier, bool select_blocks, const char* array)
{
  BEGIN_UNDO_EXCLUDE();

  vtkSMRenderViewProxy* renderModuleP = this->getRenderViewProxy();

  // Local variables for tracing
  std::string modifier = GetSelectionModifierAsString(selectionModifier);

  std::vector<int> rectVector(rect, rect + 4);

  vtkNew<vtkCollection> selectedRepresentations;
  vtkNew<vtkCollection> selectionSources;
  if (select_points)
  {
    if (!renderModuleP->SelectSurfacePoints(rect, selectedRepresentations, selectionSources,
          this->UseMultipleRepresentationSelection, selectionModifier, select_blocks, array))
    {
      END_UNDO_EXCLUDE();
      return;
    }
    SM_SCOPED_TRACE(CallFunction)
      .arg("SelectSurfacePoints")
      .arg("Rectangle", rectVector)
      .arg("Modifier", modifier.empty() ? nullptr : modifier.c_str())
      .arg("comment", qPrintable(tr("create a surface points selection")));
  }
  else
  {
    if (!renderModuleP->SelectSurfaceCells(rect, selectedRepresentations, selectionSources,
          this->UseMultipleRepresentationSelection, selectionModifier, select_blocks, array))
    {
      END_UNDO_EXCLUDE();
      return;
    }
    if (select_blocks)
    {
      SM_SCOPED_TRACE(CallFunction)
        .arg("SelectSurfaceBlocks")
        .arg("Rectangle", rectVector)
        .arg("Modifier", modifier.empty() ? nullptr : modifier.c_str())
        .arg("comment", qPrintable(tr("create a block selection")));
    }
    else
    {
      SM_SCOPED_TRACE(CallFunction)
        .arg("SelectSurfaceCells")
        .arg("Rectangle", rectVector)
        .arg("Modifier", modifier.empty() ? nullptr : modifier.c_str())
        .arg("comment", qPrintable(tr("create a surface cells selection")));
    }
  }

  END_UNDO_EXCLUDE();
  this->collectSelectionPorts(
    selectedRepresentations, selectionSources, pqOutputPorts, selectionModifier, select_blocks);
}

//-----------------------------------------------------------------------------
void pqRenderView::selectPointsOnSurface(int rect[4], int selectionModifier, const char* array)
{
  QList<pqOutputPort*> output_ports;
  this->selectOnSurfaceInternal(rect, output_ports, true, selectionModifier, false, array);
  // Fire selection event to let the world know that this view selected something.
  this->emitSelectionSignal(output_ports, selectionModifier);
}

//-----------------------------------------------------------------------------
void pqRenderView::selectPolygonPoints(vtkIntArray* polygon, int selectionModifier)
{
  QList<pqOutputPort*> output_ports;
  this->selectPolygonInternal(polygon, output_ports, true, selectionModifier, false);
  // Fire selection event to let the world know that this view selected something.
  this->emitSelectionSignal(output_ports, selectionModifier);
}

//-----------------------------------------------------------------------------
void pqRenderView::selectPolygonCells(vtkIntArray* polygon, int selectionModifier)
{
  QList<pqOutputPort*> output_ports;
  this->selectPolygonInternal(polygon, output_ports, false, selectionModifier, false);
  // Fire selection event to let the world know that this view selected something.
  this->emitSelectionSignal(output_ports, selectionModifier);
}

//-----------------------------------------------------------------------------
void pqRenderView::selectPolygonInternal(vtkIntArray* polygon, QList<pqOutputPort*>& pqOutputPorts,
  bool select_points, int selectionModifier, bool select_blocks)
{
  vtkSMRenderViewProxy* renderModuleP = this->getRenderViewProxy();
  vtkNew<vtkCollection> selectedRepresentations;
  vtkNew<vtkCollection> selectionSources;

  // Local variables for tracing
  std::string modifier = GetSelectionModifierAsString(selectionModifier);

  size_t polygonLength = static_cast<size_t>(polygon->GetNumberOfValues());
  std::vector<int> polygonVector(polygonLength);
  for (size_t i = 0; i < polygonLength; ++i)
  {
    polygonVector[i] = static_cast<int>(polygon->GetValue(static_cast<vtkIdType>(i)));
  }

  BEGIN_UNDO_EXCLUDE();
  if (select_points)
  {
    if (!renderModuleP->SelectPolygonPoints(polygon, selectedRepresentations, selectionSources,
          this->UseMultipleRepresentationSelection, selectionModifier, select_blocks))
    {
      END_UNDO_EXCLUDE();
      return;
    }
    SM_SCOPED_TRACE(CallFunction)
      .arg("SelectSurfacePoints")
      .arg("Polygon", polygonVector)
      .arg("Modifier", modifier.empty() ? nullptr : modifier.c_str())
      .arg("comment", qPrintable(tr("create a surface points polygon selection")));
  }
  else
  {
    if (!renderModuleP->SelectPolygonCells(polygon, selectedRepresentations, selectionSources,
          this->UseMultipleRepresentationSelection, selectionModifier, select_blocks))
    {
      END_UNDO_EXCLUDE();
      return;
    }
    SM_SCOPED_TRACE(CallFunction)
      .arg("SelectSurfaceCells")
      .arg("Polygon", polygonVector)
      .arg("Modifier", modifier.empty() ? nullptr : modifier.c_str())
      .arg("comment", qPrintable(tr("create a surface cells polygon selection")));
  }

  END_UNDO_EXCLUDE();
  this->collectSelectionPorts(
    selectedRepresentations, selectionSources, pqOutputPorts, selectionModifier, select_blocks);
}

//-----------------------------------------------------------------------------
void pqRenderView::selectFrustumCells(int rect[4], int selectionModifier)
{
  vtkSMRenderViewProxy* renderModuleP = this->getRenderViewProxy();
  const std::string modifier = GetSelectionModifierAsString(selectionModifier);

  vtkNew<vtkCollection> selectedRepresentations;
  vtkNew<vtkCollection> selectionSources;
  QList<pqOutputPort*> output_ports;

  BEGIN_UNDO_EXCLUDE();
  if (!renderModuleP->SelectFrustumCells(
        rect, selectedRepresentations, selectionSources, this->UseMultipleRepresentationSelection))
  {
    END_UNDO_EXCLUDE();
    this->emitSelectionSignal(output_ports, selectionModifier);
    return;
  }

  const std::vector<int> rectVector(rect, rect + 4);

  SM_SCOPED_TRACE(CallFunction)
    .arg("SelectCellsThrough")
    .arg("Rectangle", rectVector)
    .arg("Modifier", modifier.empty() ? nullptr : modifier.c_str())
    .arg("comment", qPrintable(tr("create a frustum selection of cells")));

  END_UNDO_EXCLUDE();

  this->collectSelectionPorts(
    selectedRepresentations, selectionSources, output_ports, selectionModifier, false);

  // Fire selection event to let the world know that this view selected something.
  this->emitSelectionSignal(output_ports, selectionModifier);
}

//-----------------------------------------------------------------------------
void pqRenderView::selectFrustumPoints(int rect[4], int selectionModifier)
{
  vtkSMRenderViewProxy* renderModuleP = this->getRenderViewProxy();
  const std::string modifier = GetSelectionModifierAsString(selectionModifier);

  vtkNew<vtkCollection> selectedRepresentations;
  vtkNew<vtkCollection> selectionSources;
  QList<pqOutputPort*> output_ports;

  BEGIN_UNDO_EXCLUDE();
  if (!renderModuleP->SelectFrustumPoints(
        rect, selectedRepresentations, selectionSources, this->UseMultipleRepresentationSelection))
  {
    END_UNDO_EXCLUDE();
    this->emitSelectionSignal(output_ports, selectionModifier);
    return;
  }

  const std::vector<int> rectVector(rect, rect + 4);

  SM_SCOPED_TRACE(CallFunction)
    .arg("SelectPointsThrough")
    .arg("Rectangle", rectVector)
    .arg("Modifier", modifier.empty() ? nullptr : modifier.c_str())
    .arg("comment", qPrintable(tr("create a frustum selection of points")));

  END_UNDO_EXCLUDE();

  this->collectSelectionPorts(
    selectedRepresentations, selectionSources, output_ports, selectionModifier, false);

  // Fire selection event to let the world know that this view selected something.
  this->emitSelectionSignal(output_ports, selectionModifier);
}

//-----------------------------------------------------------------------------
void pqRenderView::selectFrustumBlocks(int rect[4], int selectionModifier)
{
  vtkSMRenderViewProxy* renderModuleP = this->getRenderViewProxy();

  vtkNew<vtkCollection> selectedRepresentations;
  vtkNew<vtkCollection> selectionSources;
  QList<pqOutputPort*> output_ports;

  BEGIN_UNDO_EXCLUDE();
  if (!renderModuleP->SelectFrustumCells(
        rect, selectedRepresentations, selectionSources, this->UseMultipleRepresentationSelection))
  {
    END_UNDO_EXCLUDE();
    this->emitSelectionSignal(output_ports, selectionModifier);
    return;
  }

  END_UNDO_EXCLUDE();

  this->collectSelectionPorts(
    selectedRepresentations, selectionSources, output_ports, selectionModifier, true);

  // Fire selection event to let the world know that this view selected something.
  this->emitSelectionSignal(output_ports, selectionModifier);
}

//-----------------------------------------------------------------------------
void pqRenderView::selectBlock(int rectangle[4], int selectionModifier)
{
  bool block = this->blockSignals(true);
  QList<pqOutputPort*> opPorts;
  // block selection will create a cell based selection always
  this->selectOnSurfaceInternal(rectangle, opPorts, false, selectionModifier, true);

  this->blockSignals(block);
  this->emitSelectionSignal(opPorts, selectionModifier);
}

//-----------------------------------------------------------------------------
void pqRenderView::updateInteractionMode(pqOutputPort* opPort)
{
  // Check default mode
  vtkPVRenderViewSettings* settings = vtkPVRenderViewSettings::GetInstance();
  int defaultMode = settings->GetDefaultInteractionMode();
  if (vtkPVRenderViewSettings::ALWAYS_2D == defaultMode)
  {
    vtkSMPropertyHelper(this->getProxy(), "InteractionMode")
      .Set(vtkPVRenderView::INTERACTION_MODE_2D);
    this->getProxy()->UpdateProperty("InteractionMode", 0);
    return;
  }
  else if (vtkPVRenderViewSettings::ALWAYS_3D == defaultMode)
  {
    vtkSMPropertyHelper(this->getProxy(), "InteractionMode")
      .Set(vtkPVRenderView::INTERACTION_MODE_3D);
    this->getProxy()->UpdateProperty("InteractionMode", 1);
    return;
  }

  // (else) Set interaction mode based on extents (vtkPVRenderViewSettings::AUTOMATIC)
  vtkPVDataInformation* datainfo = opPort->getDataInformation();
  QString className = datainfo ? datainfo->GetDataClassName() : QString();

  // Regardless the type of data, see if it is flat
  double bounds[6];
  datainfo->GetBounds(bounds);

  double focal[3] = { (bounds[0] + bounds[1]) / 2, (bounds[2] + bounds[3]) / 2,
    (bounds[4] + bounds[5]) / 2 };
  double position[3] = { 0, 0, 0 };
  double viewUp[3] = { 0, 0, 0 };
  int emptyDims = 0;
  int dataAxisIdx = -1;

  double maxWidth =
    std::max({ bounds[1] - bounds[0], bounds[3] - bounds[2], bounds[5] - bounds[4] });

  if (!maxWidth)
  {
    maxWidth = 2985;
    vtkLog(INFO,
      "Bounds were not set but are needed. "
        << "Using an arbitrary value for camera position.");
  }

  // Update camera infos
  for (int i = 0; i < 3; i++)
  {
    if (bounds[i * 2 + 1] - bounds[i * 2] == 0)
    {
      emptyDims++;
      if (emptyDims == 1)
      {
        // Found an empty bound, this is either 0D, 1D or 2D
        position[i] = focal[i] + 3.35 * maxWidth;
        viewUp[(i + 2) % 3] = 1;
      }
      else if (emptyDims == 2)
      {
        // Found a second empty bound, this is either 0D, 1D
        // Replace viewUp and position set by 2D DataSet
        for (int j = 0; j < 3; j++)
        {
          position[j] = focal[j];
          viewUp[j] = 0;
        }
        position[(dataAxisIdx + 2) % 3] = focal[(dataAxisIdx + 2) % 3] + 3.35 * maxWidth;
        viewUp[(dataAxisIdx + 1) % 3] = 1;
      }
    }
    else
    {
      // Get the data axis for 1D DataSets
      dataAxisIdx = i;

      position[i] = focal[i];
    }
  }

  // FIXME: move this logic to server-manager.
  SM_SCOPED_TRACE(PropertiesModified)
    .arg(this->getProxy())
    .arg("comment", qPrintable(tr("changing interaction mode based on data extents")));
  if (emptyDims == 1 || emptyDims == 2)
  {
    // 1D or 2D DataSet
    // Update camera position
    vtkSMPropertyHelper(this->getProxy(), "CameraFocalPoint").Set(focal, 3);
    vtkSMPropertyHelper(this->getProxy(), "CameraPosition").Set(position, 3);
    vtkSMPropertyHelper(this->getProxy(), "CameraViewUp").Set(viewUp, 3);

    // Update the interaction
    vtkSMPropertyHelper(this->getProxy(), "InteractionMode")
      .Set(vtkPVRenderView::INTERACTION_MODE_2D);
    this->getProxy()->UpdateVTKObjects();
  }
  else
  {
    // 0D or 3D DataSet
    // Update the interaction
    vtkSMPropertyHelper(this->getProxy(), "InteractionMode")
      .Set(vtkPVRenderView::INTERACTION_MODE_3D);
    this->getProxy()->UpdateProperty("InteractionMode", 1);
  }
}
//-----------------------------------------------------------------------------
void pqRenderView::onInteractionModeChange()
{
  int mode = -1;
  vtkSMPropertyHelper(this->getProxy(), "InteractionMode").Get(&mode);
  if (mode != this->Internal->CurrentInteractionMode)
  {
    this->Internal->CurrentInteractionMode = mode;
    Q_EMIT updateInteractionMode(this->Internal->CurrentInteractionMode);
  }
}

//-----------------------------------------------------------------------------
void pqRenderView::onGenericFilmicPresetsChange()
{
  int presets = -1;
  vtkSMPropertyHelper(this->getProxy(), "GenericFilmicPresets").Get(&presets);
  if (presets == vtkPVRenderView::Custom)
  {
    // No presets, fall back to user specified parameters
    this->getProxy()->UpdateProperty("Contrast", 1);
    this->getProxy()->UpdateProperty("Shoulder", 1);
    this->getProxy()->UpdateProperty("MidIn", 1);
    this->getProxy()->UpdateProperty("MidOut", 1);
    this->getProxy()->UpdateProperty("HdrMax", 1);
    this->getProxy()->UpdateProperty("UseACES", 1);
  }
}

//-----------------------------------------------------------------------------
void pqRenderView::setCursor(const QCursor& cursor)
{
  this->Internal->Cursor = cursor;
  this->Internal->UpdateCursor(this->widget());
}

//-----------------------------------------------------------------------------
QCursor pqRenderView::cursor()
{
  if (auto* qvtkWidget = qobject_cast<pqQVTKWidget*>(this->widget()))
  {
    return qvtkWidget->cursorCustom();
  }

  return this->widget()->cursor();
}

//-----------------------------------------------------------------------------
void pqRenderView::setCursorVisible(bool visible)
{
  this->Internal->CursorVisible = visible;
  this->Internal->UpdateCursor(this->widget());
}
