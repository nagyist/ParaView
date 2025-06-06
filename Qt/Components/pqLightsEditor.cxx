// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-FileCopyrightText: Copyright (c) Sandia Corporation
// SPDX-License-Identifier: BSD-3-Clause
#include "pqLightsEditor.h"
#include "ui_pqLightsEditor.h"

#include "pqApplicationCore.h"
#include "pqPropertyGroupWidget.h"
#include "pqUndoStack.h"
#include "pqWidgetUtilities.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSmartPointer.h"
#include <QDoubleValidator>

class pqLightsEditor::pqInternal : public Ui::LightsEditor
{
public:
  pqInternal(pqLightsEditor* self)
  {
    this->setupUi(self);
    pqWidgetUtilities::formatChildTooltips(self);
  }
};

namespace
{
const char* LIGHT_KIT = "UseLight";
const char* KEY_LIGHT_WARMTH = "KeyLightWarmth";
const char* KEY_LIGHT_INTENSITY = "KeyLightIntensity";
const char* KEY_LIGHT_ELEVATION = "KeyLightElevation";
const char* KEY_LIGHT_AZIMUTH = "KeyLightAzimuth";
const char* FILL_LIGHT_WARMTH = "FillLightWarmth";
const char* FILL_LIGHT_K_F_RATIO = "FillLightK:F Ratio";
const char* FILL_LIGHT_ELEVATION = "FillLightElevation";
const char* FILL_LIGHT_AZIMUTH = "FillLightAzimuth";
const char* BACK_LIGHT_WARMTH = "BackLightWarmth";
const char* BACK_LIGHT_K_B_RATIO = "BackLightK:B Ratio";
const char* BACK_LIGHT_ELEVATION = "BackLightElevation";
const char* BACK_LIGHT_AZIMUTH = "BackLightAzimuth";
const char* HEAD_LIGHT_WARMTH = "HeadLightWarmth";
const char* HEAD_LIGHT_K_H_RATIO = "HeadLightK:H Ratio";
const char* MAINTAIN_LUMINANCE = "MaintainLuminance";

const char* PROPERTY_NAME[] = { LIGHT_KIT, KEY_LIGHT_WARMTH, KEY_LIGHT_INTENSITY,
  KEY_LIGHT_ELEVATION, KEY_LIGHT_AZIMUTH, FILL_LIGHT_WARMTH, FILL_LIGHT_K_F_RATIO,
  FILL_LIGHT_ELEVATION, FILL_LIGHT_AZIMUTH, BACK_LIGHT_WARMTH, BACK_LIGHT_K_B_RATIO,
  BACK_LIGHT_ELEVATION, BACK_LIGHT_AZIMUTH, HEAD_LIGHT_WARMTH, HEAD_LIGHT_K_H_RATIO,
  MAINTAIN_LUMINANCE };
const int PROPERTY_COUNT = sizeof(PROPERTY_NAME) / sizeof(PROPERTY_NAME[0]);
}

//-----------------------------------------------------------------------------
pqLightsEditor::pqLightsEditor(vtkSMProxy* proxy, vtkSMPropertyGroup* smGroup, QWidget* _parent)
  : Superclass(proxy, smGroup, _parent)
  , Internal(new pqInternal(this))
{
  Ui::LightsEditor& ui = *this->Internal;
  pqPropertyGroupWidget& pg = *this;

  pg.addPropertyLink(ui.LightKit, LIGHT_KIT);
  pg.addPropertyLink(ui.KeyLightWarmth, KEY_LIGHT_WARMTH);
  pg.addPropertyLink(ui.KeyLightIntensity, KEY_LIGHT_INTENSITY);
  pg.addPropertyLink(ui.KeyLightElevation, KEY_LIGHT_ELEVATION);
  pg.addPropertyLink(ui.KeyLightAzimuth, KEY_LIGHT_AZIMUTH);
  pg.addPropertyLink(ui.FillLightWarmth, FILL_LIGHT_WARMTH);
  pg.addPropertyLink(ui.FillLightK_F_Ratio, FILL_LIGHT_K_F_RATIO);
  pg.addPropertyLink(ui.FillLightElevation, FILL_LIGHT_ELEVATION);
  pg.addPropertyLink(ui.FillLightAzimuth, FILL_LIGHT_AZIMUTH);
  pg.addPropertyLink(ui.BackLightWarmth, BACK_LIGHT_WARMTH);
  pg.addPropertyLink(ui.BackLightK_B_Ratio, BACK_LIGHT_K_B_RATIO);
  pg.addPropertyLink(ui.BackLightElevation, BACK_LIGHT_ELEVATION);
  pg.addPropertyLink(ui.BackLightAzimuth, BACK_LIGHT_AZIMUTH);
  pg.addPropertyLink(ui.HeadLightWarmth, HEAD_LIGHT_WARMTH);
  pg.addPropertyLink(ui.HeadLightK_H_Ratio, HEAD_LIGHT_K_H_RATIO);
  pg.addPropertyLink(ui.MaintainLuminance, MAINTAIN_LUMINANCE);

  // set pqPropertyWidget links to update VTK immediately on UI change - lights are a display
  // property
  links().setUseUncheckedProperties(false);
  links().setAutoUpdateVTKObjects(true);

  // connect(ui.Close, SIGNAL(pressed()), this, SLOT(close()));
  connect(ui.Reset, SIGNAL(pressed()), this, SLOT(resetLights()));
}

//-----------------------------------------------------------------------------
pqLightsEditor::~pqLightsEditor()
{
  delete this->Internal;
  this->Internal = nullptr;
}

//-----------------------------------------------------------------------------
void pqLightsEditor::resetLights()
{
  BEGIN_UNDO_SET(tr("Restore Default Lights"));
  for (int i = 0; i < PROPERTY_COUNT; ++i)
  {
    vtkSMProperty* _property = this->propertyGroup()->GetProperty(PROPERTY_NAME[i]);
    _property->ResetToDefault();
  }
  proxy()->UpdateVTKObjects();
  Q_EMIT this->changeFinished();
  END_UNDO_SET();
}
