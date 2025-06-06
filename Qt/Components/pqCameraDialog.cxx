// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-FileCopyrightText: Copyright (c) Sandia Corporation
// SPDX-License-Identifier: BSD-3-Clause
#include "pqCameraDialog.h"
#include "ui_pqCameraDialog.h"

#include "pqScaledSpinBox.h"

// VTK / ParaView Server Manager includes.
#include "vtkCamera.h"
#include "vtkCollection.h"
#include "vtkMath.h"
#include "vtkProcessModule.h"
#include "vtkSMCameraConfigurationReader.h"
#include "vtkSMCameraConfigurationWriter.h"
#include "vtkSMCameraLink.h"
#include "vtkSMProperty.h"
#include "vtkSMProxy.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSmartPointer.h"
#include "vtkTransform.h"

#include "vtkPVXMLElement.h"
#include "vtkPVXMLParser.h"

// Qt includes.
#include <QDebug>
#include <QPointer>
#include <QString>
#include <QToolButton>

// ParaView Client includes.
#include "pqActiveObjects.h"
#include "pqApplicationCore.h"
#include "pqCustomViewpointButtonDialog.h"
#include "pqFileDialog.h"
#include "pqInteractiveViewLink.h"
#include "pqLinksModel.h"
#include "pqPropertyLinks.h"
#include "pqRenderView.h"
#include "pqSettings.h"
#include "pqWidgetUtilities.h"

// STL
#include <sstream>
#include <string>
#include <type_traits>

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
#define QT_ENDL endl
#else
#define QT_ENDL Qt::endl
#endif

#define pqErrorMacro(estr)                                                                         \
  qDebug() << "Error in:" << QT_ENDL << __FILE__ << ", line " << __LINE__ << QT_ENDL << "" estr    \
           << QT_ENDL;

namespace
{

QStringList getListOfStrings(pqSettings* settings, const QString& defaultTxt, int min, int max)
{
  QStringList val;
  for (int cc = 0; cc < max; ++cc)
  {
    const QString key = QString::number(cc);
    if (cc < min || settings->contains(key))
    {
      val << settings->value(key, defaultTxt).toString();
    }
    else
    {
      break;
    }
  }
  return val;
}
};

//=============================================================================
class pqCameraDialogInternal : public Ui::pqCameraDialog
{
  QVector<QPointer<QToolButton>> CustomViewpointButtons;
  QPointer<QToolButton> PlusButton;

public:
  QPointer<pqRenderView> RenderModule;
  pqPropertyLinks CameraLinks;

  pqCameraDialogInternal()
  {
    // Add + bouton
    this->PlusButton = new QToolButton();
    this->PlusButton->setObjectName("AddButton");
    this->PlusButton->setToolTip(pqWidgetUtilities::formatTooltip(
      QCoreApplication::translate("pqCameraDialog", "Add Current Viewpoint")));
    this->PlusButton->setIcon(QIcon(":/QtWidgets/Icons/pqPlus.svg"));
    this->PlusButton->setMinimumSize(QSize(34, 34));
  }

  ~pqCameraDialogInternal() { delete this->PlusButton; }

  void updateCustomViewpointButtons(::pqCameraDialog* self)
  {
    QStringList toolTips = self->CustomViewpointToolTips();

    // Remove supplemental buttons
    this->PlusButton->disconnect();
    this->customViewpointGridLayout->removeWidget(this->PlusButton);

    for (int cc = this->CustomViewpointButtons.size(); cc > toolTips.size(); cc--)
    {
      this->customViewpointGridLayout->removeWidget(this->CustomViewpointButtons[cc - 1]);
      delete this->CustomViewpointButtons[cc - 1];
    }
    if (this->CustomViewpointButtons.size() > toolTips.size())
    {
      this->CustomViewpointButtons.resize(toolTips.size());
    }

    // add / change remaining buttons.
    for (int cc = 0; cc < toolTips.size(); ++cc)
    {
      if (this->CustomViewpointButtons.size() > cc)
      {
        this->CustomViewpointButtons[cc]->setToolTip(toolTips[cc]);
      }
      else
      {
        QToolButton* tb = new QToolButton(self);
        tb->setObjectName(QString("customViewpoint%1").arg(cc));
        tb->setText(QString::number(cc + 1));
        tb->setToolTip(toolTips[cc]);
        tb->setProperty("pqCameraDialog_INDEX", cc);
        tb->setMinimumSize(QSize(34, 34));
        self->connect(tb, SIGNAL(clicked()), SLOT(applyCustomViewpoint()));
        this->CustomViewpointButtons.push_back(tb);
        this->customViewpointGridLayout->addWidget(tb, cc / 6, cc % 6);
      }
    }

    // Add Plus Button if needed
    if (toolTips.size() < pqCustomViewpointButtonDialog::MAXIMUM_NUMBER_OF_ITEMS)
    {
      self->connect(
        this->PlusButton, SIGNAL(clicked()), SLOT(addCurrentViewpointToCustomViewpoints()));
      this->customViewpointGridLayout->addWidget(
        this->PlusButton, toolTips.size() / 6, toolTips.size() % 6);
    }
  }
};

//-----------------------------------------------------------------------------
pqCameraDialog::pqCameraDialog(QWidget* _p /*=nullptr*/, Qt::WindowFlags f /*=0*/)
  : pqDialog(_p, f)
{
  this->Internal = new pqCameraDialogInternal;
  this->Internal->setupUi(this);
  pqWidgetUtilities::formatChildTooltips(this);

  this->setUndoLabel("Camera");

  QObject::connect(
    this->Internal->viewXPlus, SIGNAL(clicked()), this, SLOT(resetViewDirectionPosX()));
  QObject::connect(
    this->Internal->viewXMinus, SIGNAL(clicked()), this, SLOT(resetViewDirectionNegX()));
  QObject::connect(
    this->Internal->viewYPlus, SIGNAL(clicked()), this, SLOT(resetViewDirectionPosY()));
  QObject::connect(
    this->Internal->viewYMinus, SIGNAL(clicked()), this, SLOT(resetViewDirectionNegY()));
  QObject::connect(
    this->Internal->viewZPlus, SIGNAL(clicked()), this, SLOT(resetViewDirectionPosZ()));
  QObject::connect(
    this->Internal->viewZMinus, SIGNAL(clicked()), this, SLOT(resetViewDirectionNegZ()));
  QObject::connect(
    this->Internal->viewIsometric, SIGNAL(clicked()), this, SLOT(applyIsometricView()));

  QObject::connect(this->Internal->AutoResetCenterOfRotation, SIGNAL(toggled(bool)), this,
    SLOT(resetRotationCenterWithCamera()));

  QObject::connect(
    this->Internal->rollPlusButton, SIGNAL(clicked()), this, SLOT(applyCameraRollPlus()));
  QObject::connect(
    this->Internal->rollMinusButton, SIGNAL(clicked()), this, SLOT(applyCameraRollMinus()));
  QObject::connect(
    this->Internal->elevationPlusButton, SIGNAL(clicked()), this, SLOT(applyCameraElevationPlus()));
  QObject::connect(this->Internal->elevationMinusButton, SIGNAL(clicked()), this,
    SLOT(applyCameraElevationMinus()));
  QObject::connect(
    this->Internal->azimuthPlusButton, SIGNAL(clicked()), this, SLOT(applyCameraAzimuthPlus()));
  QObject::connect(
    this->Internal->azimuthMinusButton, SIGNAL(clicked()), this, SLOT(applyCameraAzimuthMinus()));
  QObject::connect(
    this->Internal->zoomInButton, SIGNAL(clicked()), this, SLOT(applyCameraZoomIn()));
  QObject::connect(
    this->Internal->zoomOutButton, SIGNAL(clicked()), this, SLOT(applyCameraZoomOut()));

  QObject::connect(this->Internal->saveCameraConfiguration, SIGNAL(clicked()), this,
    SLOT(saveCameraConfiguration()));

  QObject::connect(this->Internal->loadCameraConfiguration, SIGNAL(clicked()), this,
    SLOT(loadCameraConfiguration()));

  QObject::connect(this->Internal->configureCustomViewpoints, SIGNAL(clicked()), this,
    SLOT(configureCustomViewpoints()));

  pqSettings* settings = pqApplicationCore::instance()->settings();
  this->connect(settings, SIGNAL(modified()), SLOT(updateCustomViewpointButtons()));

  QObject::connect(this->Internal->interactiveViewLinkComboBox, SIGNAL(currentIndexChanged(int)),
    this, SLOT(updateInteractiveViewLinkWidgets()));

  QObject::connect(this->Internal->interactiveViewLinkBackground, SIGNAL(toggled(bool)), this,
    SLOT(setInteractiveViewLinkBackground(bool)));

  QObject::connect(this->Internal->interactiveViewLinkOpacity, SIGNAL(valueChanged(double)), this,
    SLOT(setInteractiveViewLinkOpacity(double)));

  QObject::connect(&pqActiveObjects::instance(), SIGNAL(viewChanged(pqView*)), this,
    SLOT(setRenderModule(pqView*)));

  // load custom view buttons with any tool tips set by the user in a previous
  // session.
  this->updateCustomViewpointButtons();
}

//-----------------------------------------------------------------------------
pqCameraDialog::~pqCameraDialog()
{
  delete this->Internal;
}

//-----------------------------------------------------------------------------
void pqCameraDialog::setRenderModule(pqView* view)
{
  pqRenderView* renderView = qobject_cast<pqRenderView*>(view);
  if (renderView)
  {
    this->Internal->RenderModule = renderView;
    this->setupGUI();
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::setupGUI()
{
  if (this->Internal->RenderModule)
  {
    vtkSMRenderViewProxy* proxy = this->Internal->RenderModule->getRenderViewProxy();
    proxy->SynchronizeCameraProperties();

    this->Internal->CameraLinks.removeAllPropertyLinks();
    this->Internal->CameraLinks.addPropertyLink(this->Internal->position0, "text2",
      SIGNAL(textChangedAndEditingFinished()), proxy, proxy->GetProperty("CameraPosition"), 0);
    this->Internal->CameraLinks.addPropertyLink(this->Internal->position1, "text2",
      SIGNAL(textChangedAndEditingFinished()), proxy, proxy->GetProperty("CameraPosition"), 1);
    this->Internal->CameraLinks.addPropertyLink(this->Internal->position2, "text2",
      SIGNAL(textChangedAndEditingFinished()), proxy, proxy->GetProperty("CameraPosition"), 2);

    this->Internal->CameraLinks.addPropertyLink(this->Internal->focalPoint0, "text2",
      SIGNAL(textChangedAndEditingFinished()), proxy, proxy->GetProperty("CameraFocalPoint"), 0);
    this->Internal->CameraLinks.addPropertyLink(this->Internal->focalPoint1, "text2",
      SIGNAL(textChangedAndEditingFinished()), proxy, proxy->GetProperty("CameraFocalPoint"), 1);
    this->Internal->CameraLinks.addPropertyLink(this->Internal->focalPoint2, "text2",
      SIGNAL(textChangedAndEditingFinished()), proxy, proxy->GetProperty("CameraFocalPoint"), 2);

    this->Internal->CameraLinks.addPropertyLink(this->Internal->viewUp0, "text2",
      SIGNAL(textChangedAndEditingFinished()), proxy, proxy->GetProperty("CameraViewUp"), 0);
    this->Internal->CameraLinks.addPropertyLink(this->Internal->viewUp1, "text2",
      SIGNAL(textChangedAndEditingFinished()), proxy, proxy->GetProperty("CameraViewUp"), 1);
    this->Internal->CameraLinks.addPropertyLink(this->Internal->viewUp2, "text2",
      SIGNAL(textChangedAndEditingFinished()), proxy, proxy->GetProperty("CameraViewUp"), 2);

    this->Internal->CameraLinks.addPropertyLink(this->Internal->CenterX, "text2",
      SIGNAL(textChangedAndEditingFinished()), proxy, proxy->GetProperty("CenterOfRotation"), 0);
    this->Internal->CameraLinks.addPropertyLink(this->Internal->CenterY, "text2",
      SIGNAL(textChangedAndEditingFinished()), proxy, proxy->GetProperty("CenterOfRotation"), 1);
    this->Internal->CameraLinks.addPropertyLink(this->Internal->CenterZ, "text2",
      SIGNAL(textChangedAndEditingFinished()), proxy, proxy->GetProperty("CenterOfRotation"), 2);

    this->Internal->CameraLinks.addPropertyLink(this->Internal->rotationFactor, "value",
      SIGNAL(valueChanged(double)), proxy, proxy->GetProperty("RotationFactor"), 0);

    this->Internal->CameraLinks.addPropertyLink(this->Internal->viewAngle, "value",
      SIGNAL(valueChanged(double)), proxy, proxy->GetProperty("CameraViewAngle"), 0);
    this->Internal->CameraLinks.addPropertyLink(this->Internal->eyeAngle, "value",
      SIGNAL(valueChanged(double)), proxy, proxy->GetProperty("EyeAngle"), 0);

    this->Internal->CameraLinks.addPropertyLink(this->Internal->focalDisk, "value",
      SIGNAL(valueChanged(double)), proxy, proxy->GetProperty("CameraFocalDisk"), 0);
    this->Internal->CameraLinks.addPropertyLink(this->Internal->focalDistance, "value",
      SIGNAL(valueChanged(double)), proxy, proxy->GetProperty("CameraFocalDistance"), 0);

    QObject::connect(&this->Internal->CameraLinks, SIGNAL(qtWidgetChanged()),
      this->Internal->RenderModule, SLOT(render()));

    this->Internal->AutoResetCenterOfRotation->setCheckState(
      this->Internal->RenderModule->getResetCenterWithCamera() ? Qt::Checked : Qt::Unchecked);

    // Interactive View Link Options
    pqLinksModel* model = pqApplicationCore::instance()->getLinksModel();
    vtkNew<vtkCollection> cameraLinks;
    model->FindLinksFromProxy(proxy, vtkSMLink::INPUT, cameraLinks.Get());

    // For each found link
    vtkSMSessionProxyManager* pxm = pqActiveObjects::instance().proxyManager();
    this->Internal->interactiveViewLinkComboBox->clear();
    for (int i = 0; i < cameraLinks->GetNumberOfItems(); i++)
    {
      // check if it is a camera link
      vtkSMCameraLink* cameraLink = vtkSMCameraLink::SafeDownCast(cameraLinks->GetItemAsObject(i));
      if (cameraLink != nullptr)
      {
        const char* linkName = pxm->GetRegisteredLinkName(cameraLink);
        if (model->hasInteractiveViewLink(linkName))
        {
          this->Internal->interactiveViewLinkComboBox->addItem(linkName,
            QVariant::fromValue(static_cast<void*>(model->getInteractiveViewLink(linkName))));
        }
      }
    }
    if (cameraLinks->GetNumberOfItems() == 0)
    {
      this->updateInteractiveViewLinkWidgets();
    }
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::updateInteractiveViewLinkWidgets()
{
  const bool enabled = (this->Internal->interactiveViewLinkComboBox->count() > 0);

  this->Internal->interactiveViewLinkComboBox->setEnabled(enabled);
  this->Internal->interactiveViewLinkBackground->setEnabled(enabled);
  this->Internal->interactiveViewLinkOpacity->setEnabled(enabled);

  if (enabled)
  {
    pqInteractiveViewLink* ivLink = static_cast<pqInteractiveViewLink*>(
      this->Internal->interactiveViewLinkComboBox
        ->itemData(this->Internal->interactiveViewLinkComboBox->currentIndex())
        .value<void*>());

    bool block = this->Internal->interactiveViewLinkOpacity->blockSignals(true);
    this->Internal->interactiveViewLinkOpacity->setValue(ivLink->getOpacity());
    this->Internal->interactiveViewLinkOpacity->blockSignals(block);

    block = this->Internal->interactiveViewLinkBackground->blockSignals(true);
    this->Internal->interactiveViewLinkBackground->setChecked(
      ivLink->getHideLinkedViewBackground());
    this->Internal->interactiveViewLinkBackground->blockSignals(block);
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::setInteractiveViewLinkOpacity(double value)
{
  pqInteractiveViewLink* ivLink = static_cast<pqInteractiveViewLink*>(
    this->Internal->interactiveViewLinkComboBox
      ->itemData(this->Internal->interactiveViewLinkComboBox->currentIndex())
      .value<void*>());
  ivLink->setOpacity(value);
}

//-----------------------------------------------------------------------------
void pqCameraDialog::setInteractiveViewLinkBackground(bool hideBackground)
{
  pqInteractiveViewLink* ivLink = static_cast<pqInteractiveViewLink*>(
    this->Internal->interactiveViewLinkComboBox
      ->itemData(this->Internal->interactiveViewLinkComboBox->currentIndex())
      .value<void*>());
  ivLink->setHideLinkedViewBackground(hideBackground);
}

//-----------------------------------------------------------------------------
void pqCameraDialog::SetCameraGroupsEnabled(bool enabled)
{
  auto& internal = (*this->Internal);
  internal.viewXMinus->setEnabled(enabled);
  internal.viewXPlus->setEnabled(enabled);
  internal.viewYMinus->setEnabled(enabled);
  internal.viewYPlus->setEnabled(enabled);
  internal.viewZMinus->setEnabled(enabled);
  internal.viewZPlus->setEnabled(enabled);

  internal.customViewpointGridLayout->setEnabled(enabled);
  internal.configureCustomViewpoints->setEnabled(enabled);

  internal.CenterX->setEnabled(enabled);
  internal.CenterY->setEnabled(enabled);
  internal.CenterZ->setEnabled(enabled);
  internal.AutoResetCenterOfRotation->setEnabled(enabled);

  internal.rotationFactor->setEnabled(enabled);

  internal.position0->setEnabled(enabled);
  internal.position1->setEnabled(enabled);
  internal.position2->setEnabled(enabled);
  internal.focalPoint0->setEnabled(enabled);
  internal.focalPoint1->setEnabled(enabled);
  internal.focalPoint2->setEnabled(enabled);
  internal.viewUp0->setEnabled(enabled);
  internal.viewUp1->setEnabled(enabled);
  internal.viewUp2->setEnabled(enabled);
  internal.viewAngle->setEnabled(enabled);
  internal.loadCameraConfiguration->setEnabled(enabled);
  internal.saveCameraConfiguration->setEnabled(enabled);

  internal.rollPlusButton->setEnabled(enabled);
  internal.rollMinusButton->setEnabled(enabled);
  internal.rollAngle->setEnabled(enabled);
  internal.elevationPlusButton->setEnabled(enabled);
  internal.elevationMinusButton->setEnabled(enabled);
  internal.elevationAngle->setEnabled(enabled);
  internal.azimuthPlusButton->setEnabled(enabled);
  internal.azimuthMinusButton->setEnabled(enabled);
  internal.azimuthAngle->setEnabled(enabled);
  internal.zoomInButton->setEnabled(enabled);
  internal.zoomFactor->setEnabled(enabled);
  internal.zoomOutButton->setEnabled(enabled);
}

//-----------------------------------------------------------------------------
void pqCameraDialog::resetViewDirection(
  double look_x, double look_y, double look_z, double up_x, double up_y, double up_z)
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->resetViewDirection(look_x, look_y, look_z, up_x, up_y, up_z);
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::resetViewDirectionPosX()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->resetViewDirectionToPositiveX();
  }
}
//-----------------------------------------------------------------------------
void pqCameraDialog::resetViewDirectionNegX()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->resetViewDirectionToNegativeX();
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::resetViewDirectionPosY()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->resetViewDirectionToPositiveY();
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::resetViewDirectionNegY()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->resetViewDirectionToNegativeY();
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::resetViewDirectionPosZ()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->resetViewDirectionToPositiveZ();
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::resetViewDirectionNegZ()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->resetViewDirectionToNegativeZ();
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::applyIsometricView()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->applyIsometricView();
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::applyCameraRollPlus()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->adjustRoll(this->Internal->rollAngle->value());
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::applyCameraRollMinus()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->adjustRoll(-this->Internal->rollAngle->value());
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::applyCameraElevationPlus()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->adjustElevation(this->Internal->elevationAngle->value());
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::applyCameraElevationMinus()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->adjustElevation(-this->Internal->elevationAngle->value());
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::applyCameraAzimuthPlus()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->adjustAzimuth(this->Internal->azimuthAngle->value());
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::applyCameraAzimuthMinus()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->adjustAzimuth(-this->Internal->azimuthAngle->value());
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::applyCameraZoomIn()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->adjustZoom(this->Internal->zoomFactor->value());
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::applyCameraZoomOut()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->adjustZoom(1.0 / this->Internal->zoomFactor->value());
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::resetRotationCenterWithCamera()
{
  if (this->Internal->RenderModule)
  {
    this->Internal->RenderModule->setResetCenterWithCamera(
      this->Internal->AutoResetCenterOfRotation->checkState() == Qt::Checked);
  }
}

//-----------------------------------------------------------------------------
void pqCameraDialog::configureCustomViewpoints()
{
  if (pqCameraDialog::configureCustomViewpoints(
        this, this->Internal->RenderModule->getRenderViewProxy()))
  {
    this->updateCustomViewpointButtons();
  }
}

//-----------------------------------------------------------------------------
bool pqCameraDialog::configureCustomViewpoints(
  QWidget* parentWidget, vtkSMRenderViewProxy* viewProxy)
{
  QStringList toolTips = pqCameraDialog::CustomViewpointToolTips();
  QStringList configs = pqCameraDialog::CustomViewpointConfigurations();

  // grab the current camera configuration.
  std::ostringstream os;

  vtkNew<vtkSMCameraConfigurationWriter> writer;
  writer->SetRenderViewProxy(viewProxy);
  writer->WriteConfiguration(os);

  QString currentConfig(os.str().c_str());

  // user modifies the configuration
  pqCustomViewpointButtonDialog dialog(
    parentWidget, Qt::WindowFlags{}, toolTips, configs, currentConfig);
  if (dialog.exec() == QDialog::Accepted)
  {
    // save the new configuration into the app wide settings.
    configs = dialog.getConfigurations();
    pqSettings* settings = pqApplicationCore::instance()->settings();
    settings->beginGroup("CustomViewButtons");
    settings->beginGroup("Configurations");
    settings->remove(""); // remove all items in the group.
    int index = 0;
    for (const QString& config : configs)
    {
      settings->setValue(QString::number(index++), config);
    }
    settings->endGroup();

    toolTips = dialog.getToolTips();
    settings->beginGroup("ToolTips");
    settings->remove(""); // remove all items in the group.
    index = 0;
    for (const QString& toolTip : toolTips)
    {
      settings->setValue(QString::number(index++), toolTip);
    }
    settings->endGroup();
    settings->endGroup();
    settings->alertSettingsModified();
    return true;
  }
  return false;
}

//-----------------------------------------------------------------------------
void pqCameraDialog::addCurrentViewpointToCustomViewpoints()
{
  if (pqCameraDialog::addCurrentViewpointToCustomViewpoints(
        this->Internal->RenderModule->getRenderViewProxy()))
  {
    this->updateCustomViewpointButtons();
  }
}

//-----------------------------------------------------------------------------
bool pqCameraDialog::addCurrentViewpointToCustomViewpoints(vtkSMRenderViewProxy* viewProxy)
{
  if (!viewProxy)
  {
    return false;
  }

  // grab the current camera configuration.
  std::ostringstream os;
  vtkNew<vtkSMCameraConfigurationWriter> writer;
  writer->SetRenderViewProxy(viewProxy);
  writer->WriteConfiguration(os);

  // load the existing button configurations from the app wide settings.
  QStringList configs = pqCameraDialog::CustomViewpointConfigurations();

  // Add current viewpoint config to setting
  pqSettings* settings = pqApplicationCore::instance()->settings();
  settings->beginGroup("CustomViewButtons");
  settings->beginGroup("Configurations");
  settings->setValue(QString::number(configs.size()), os.str().c_str());
  settings->endGroup();
  settings->beginGroup("ToolTips");
  settings->setValue(
    QString::number(configs.size()), tr("Current Viewpoint %1").arg(configs.size() + 1));
  settings->endGroup();
  settings->endGroup();
  settings->alertSettingsModified();
  return true;
}

//-----------------------------------------------------------------------------
void pqCameraDialog::applyCustomViewpoint()
{
  int buttonId = -1;
  if (QObject* asender = this->sender())
  {
    buttonId = asender->property("pqCameraDialog_INDEX").toInt();
  }
  else
  {
    return;
  }

  if (pqCameraDialog::applyCustomViewpoint(
        buttonId, this->Internal->RenderModule->getRenderViewProxy()))
  {
    // camera configuration has been modified update the scene.
    this->Internal->RenderModule->render();
  }
}

//-----------------------------------------------------------------------------
bool pqCameraDialog::applyCustomViewpoint(int CustomViewpointIndex, vtkSMRenderViewProxy* viewProxy)
{
  if (!viewProxy)
  {
    return false;
  }
  pqSettings* settings = pqApplicationCore::instance()->settings();
  settings->beginGroup("CustomViewButtons");
  settings->beginGroup("Configurations");
  QString config = settings->value(QString::number(CustomViewpointIndex), "").toString();
  settings->endGroup();
  settings->endGroup();
  if (config.isEmpty())
  {
    return false;
  }

  vtkNew<vtkPVXMLParser> parser;
  parser->InitializeParser();
  parser->ParseChunk(config.toUtf8().data(), static_cast<unsigned int>(config.size()));
  parser->CleanupParser();

  vtkPVXMLElement* xmlStream = parser->GetRootElement();
  if (!xmlStream)
  {
    pqErrorMacro("Invalid XML in custom view button configuration.");
    return false;
  }

  vtkNew<vtkSMCameraConfigurationReader> reader;
  reader->SetRenderViewProxy(viewProxy);
  if (reader->ReadConfiguration(xmlStream) == 0)
  {
    pqErrorMacro(<< "Invalid XML in custom view button " << CustomViewpointIndex
                 << " configuration.");
    return false;
  }

  return true;
}

//-----------------------------------------------------------------------------
bool pqCameraDialog::deleteCustomViewpoint(
  int CustomViewpointIndex, vtkSMRenderViewProxy* viewProxy)
{
  if (!viewProxy)
  {
    return false;
  }
  QStringList toolTips = pqCameraDialog::CustomViewpointToolTips();
  if (CustomViewpointIndex >= toolTips.size())
  {
    return false;
  }

  pqSettings* settings = pqApplicationCore::instance()->settings();
  settings->beginGroup("CustomViewButtons");
  settings->beginGroup("Configurations");
  for (int i = 0; i < toolTips.size() - 1; i++)
  {
    if (i < CustomViewpointIndex)
    {
      continue;
    }
    settings->setValue(QString::number(i), settings->value(QString::number(i + 1)));
  }
  settings->remove(QString::number(toolTips.size() - 1));
  settings->endGroup();
  settings->beginGroup("ToolTips");
  for (int i = 0; i < toolTips.size() - 1; i++)
  {
    if (i < CustomViewpointIndex)
    {
      continue;
    }
    settings->setValue(QString::number(i), settings->value(QString::number(i + 1)));
  }
  settings->remove(QString::number(toolTips.size() - 1));
  settings->endGroup();
  settings->endGroup();
  settings->alertSettingsModified();
  return true;
}

//-----------------------------------------------------------------------------
bool pqCameraDialog::setToCurrentViewpoint(
  int CustomViewpointIndex, vtkSMRenderViewProxy* viewProxy)
{
  if (!viewProxy)
  {
    return false;
  }

  // grab the current camera configuration.
  std::ostringstream os;
  vtkNew<vtkSMCameraConfigurationWriter> writer;
  writer->SetRenderViewProxy(viewProxy);
  writer->WriteConfiguration(os);

  // load the existing button configurations from the app wide settings.
  QStringList configs = pqCameraDialog::CustomViewpointConfigurations();

  // Add current viewpoint config to setting
  pqSettings* settings = pqApplicationCore::instance()->settings();
  settings->beginGroup("CustomViewButtons");
  settings->beginGroup("Configurations");
  settings->setValue(QString::number(CustomViewpointIndex), os.str().c_str());
  settings->endGroup();
  settings->endGroup();
  settings->alertSettingsModified();
  return true;
}

//-----------------------------------------------------------------------------
void pqCameraDialog::saveCameraConfiguration()
{
  vtkSMCameraConfigurationWriter* writer = vtkSMCameraConfigurationWriter::New();
  writer->SetRenderViewProxy(this->Internal->RenderModule->getRenderViewProxy());

  QString filters =
    QString("%1 (*%2);;").arg(writer->GetFileDescription()).arg(writer->GetFileExtension()) +
    tr("All Files") + " (*.*)";

  pqFileDialog dialog(nullptr, this, tr("Save Camera Configuration"), "", filters, false);
  dialog.setFileMode(pqFileDialog::AnyFile);

  if (dialog.exec() == QDialog::Accepted)
  {
    QString filename(dialog.getSelectedFiles()[0]);

    int ok = writer->WriteConfiguration(filename.toStdString().c_str());
    if (!ok)
    {
      pqErrorMacro("Failed to save the camera configuration.");
    }
  }

  writer->Delete();
}

//-----------------------------------------------------------------------------
void pqCameraDialog::loadCameraConfiguration()
{
  vtkSMCameraConfigurationReader* reader = vtkSMCameraConfigurationReader::New();
  reader->SetRenderViewProxy(this->Internal->RenderModule->getRenderViewProxy());

  QString filters =
    QString("%1 (*%2);;").arg(reader->GetFileDescription()).arg(reader->GetFileExtension()) +
    tr("All Files") + " (*.*)";

  pqFileDialog dialog(nullptr, this, tr("Load Camera Configuration"), "", filters, false);
  dialog.setFileMode(pqFileDialog::ExistingFile);

  if (dialog.exec() == QDialog::Accepted)
  {
    QString filename;
    filename = dialog.getSelectedFiles()[0];

    int ok = reader->ReadConfiguration(filename.toStdString().c_str());
    if (!ok)
    {
      pqErrorMacro("Failed to load the camera configuration.");
    }

    // Update the scene with the new camera settings.
    this->Internal->RenderModule->render();
  }

  reader->Delete();
}

//-----------------------------------------------------------------------------
QStringList pqCameraDialog::CustomViewpointConfigurations()
{
  // Recover configurations from settings
  pqSettings* settings = pqApplicationCore::instance()->settings();
  settings->beginGroup("CustomViewButtons");
  settings->beginGroup("Configurations");
  QStringList configs = getListOfStrings(settings, pqCustomViewpointButtonDialog::DEFAULT_TOOLTIP,
    pqCustomViewpointButtonDialog::MINIMUM_NUMBER_OF_ITEMS,
    pqCustomViewpointButtonDialog::MAXIMUM_NUMBER_OF_ITEMS);
  settings->endGroup();
  settings->endGroup();
  return configs;
}

//-----------------------------------------------------------------------------
QStringList pqCameraDialog::CustomViewpointToolTips()
{
  // Recover tooltTips from settings
  pqSettings* settings = pqApplicationCore::instance()->settings();
  settings->beginGroup("CustomViewButtons");
  settings->beginGroup("ToolTips");
  QStringList toolTips = getListOfStrings(settings, pqCustomViewpointButtonDialog::DEFAULT_TOOLTIP,
    pqCustomViewpointButtonDialog::MINIMUM_NUMBER_OF_ITEMS,
    pqCustomViewpointButtonDialog::MAXIMUM_NUMBER_OF_ITEMS);
  settings->endGroup();
  settings->endGroup();
  return toolTips;
}

//-----------------------------------------------------------------------------
void pqCameraDialog::updateCustomViewpointButtons()
{
  this->Internal->updateCustomViewpointButtons(this);
}
