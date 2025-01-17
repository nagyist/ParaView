// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-FileCopyrightText: Copyright (c) Sandia Corporation
// SPDX-License-Identifier: BSD-3-Clause
#include "pqColorChooserButtonWithPalettes.h"

#include "pqActiveObjects.h"
#include "pqServer.h"
#include "pqSetName.h"
#include "vtkSMDoubleVectorProperty.h"
#include "vtkSMPropertyIterator.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMSettingsProxy.h"

// Qt Includes.
#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QIcon>
#include <QMenu>

#include <QCoreApplication>
#include <cassert>

//-----------------------------------------------------------------------------
pqColorChooserButtonWithPalettes::pqColorChooserButtonWithPalettes(QWidget* parentObject)
  : Superclass(parentObject)
{
  // Setup a popup menu.
  QMenu* popupMenu = new QMenu(this);
  popupMenu << pqSetName("ColorPaletteMenu");
  this->setMenu(popupMenu);
  this->connect(popupMenu, SIGNAL(aboutToShow()), SLOT(updateMenu()));
  this->connect(popupMenu, SIGNAL(triggered(QAction*)), SLOT(actionTriggered(QAction*)));

  this->setPopupMode(QToolButton::MenuButtonPopup);

  // although unnecessary, fillup the menu here so that test playback
  // can work correctly.
  this->updateMenu();
}

//-----------------------------------------------------------------------------
pqColorChooserButtonWithPalettes::~pqColorChooserButtonWithPalettes() = default;

//-----------------------------------------------------------------------------
vtkSMSettingsProxy* pqColorChooserButtonWithPalettes::colorPalette() const
{
  pqServer* server = pqActiveObjects::instance().activeServer();
  auto pxm = server ? server->proxyManager() : nullptr;
  return (
    pxm ? vtkSMSettingsProxy::SafeDownCast(pxm->GetProxy("settings", "ColorPalette")) : nullptr);
}

//-----------------------------------------------------------------------------
void pqColorChooserButtonWithPalettes::updateMenu()
{
  QMenu* popupMenu = this->menu();
  assert(popupMenu);

  popupMenu->clear();

  delete this->ActionGroup;
  this->ActionGroup = new QActionGroup(popupMenu);

  // Add palettes colors
  vtkSMProxy* cp = this->colorPalette();
  if (!cp)
  {
    return;
  }

  QString paletteColorName;

  pqColorPaletteLinkHelper* helper = this->findChild<pqColorPaletteLinkHelper*>();
  if (helper)
  {
    paletteColorName = helper->selectedPaletteColor();
  }

  vtkSMPropertyIterator* iter = cp->NewPropertyIterator();
  for (iter->Begin(); !iter->IsAtEnd(); iter->Next())
  {
    vtkSMDoubleVectorProperty* dvp = vtkSMDoubleVectorProperty::SafeDownCast(iter->GetProperty());

    if (dvp && dvp->GetNumberOfElements() == 3)
    {
      QColor qcolor;
      qcolor.setRgbF(dvp->GetElement(0), dvp->GetElement(1), dvp->GetElement(2));

      QAction* action = popupMenu->addAction(this->renderColorSwatch(qcolor),
        QCoreApplication::translate("ServerManagerXML", dvp->GetXMLLabel()));
      action << pqSetName(iter->GetKey());
      action->setData(QVariant(iter->GetKey()));
      action->setCheckable(true);
      if (paletteColorName == iter->GetKey())
      {
        action->setChecked(true);
      }
      this->ActionGroup->addAction(action);
    }
  }

  // Add B&W stock colors
  popupMenu->addSeparator();
  QColor black(Qt::black);
  popupMenu->addAction(this->renderColorSwatch(black), tr("Black"))->setData(black);
  QColor white(Qt::white);
  popupMenu->addAction(this->renderColorSwatch(white), tr("White"))->setData(white);

  iter->Delete();
}

//-----------------------------------------------------------------------------
void pqColorChooserButtonWithPalettes::actionTriggered(QAction* action)
{
  QColor color;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  QMetaType::Type typeId = static_cast<QMetaType::Type>(action->data().type());
#else
  auto typeId = action->data().typeId();
#endif
  if (typeId == QMetaType::QString)
  {
    QString prop_name = action->data().toString();
    vtkSMProxy* globalProps = this->colorPalette();
    assert(globalProps);

    vtkSMDoubleVectorProperty* dvp =
      vtkSMDoubleVectorProperty::SafeDownCast(globalProps->GetProperty(prop_name.toUtf8().data()));
    color.setRgbF(dvp->GetElement(0), dvp->GetElement(1), dvp->GetElement(2));
    pqColorPaletteLinkHelper* helper = this->findChild<pqColorPaletteLinkHelper*>();
    if (helper)
    {
      helper->setSelectedPaletteColor(prop_name);
    }
  }
  else if (typeId == QMetaType::QColor)
  {
    color = action->data().value<QColor>();
  }

  this->setChosenColor(color);
}

//============================================================================
//  CLASS: pqColorPaletteLinkHelper
//============================================================================

//-----------------------------------------------------------------------------
pqColorPaletteLinkHelper::pqColorPaletteLinkHelper(
  pqColorChooserButtonWithPalettes* button, vtkSMProxy* smproxy, const char* smproperty)
  : Superclass(button)
  , SMProxy(smproxy)
  , SMPropertyName(smproperty)
{
  assert(button && smproperty && smproxy);
}

//-----------------------------------------------------------------------------
pqColorPaletteLinkHelper::~pqColorPaletteLinkHelper() = default;

//-----------------------------------------------------------------------------
void pqColorPaletteLinkHelper::setSelectedPaletteColor(const QString& colorName)
{
  pqColorChooserButtonWithPalettes* button =
    qobject_cast<pqColorChooserButtonWithPalettes*>(this->parent());
  assert(button);

  vtkSMSettingsProxy* palette = button->colorPalette();
  if (palette && this->SMProxy)
  {
    auto oldLink =
      palette->GetSourcePropertyName(this->SMProxy, this->SMPropertyName.toUtf8().data());
    if (oldLink)
    {
      palette->RemoveLink(oldLink, this->SMProxy, this->SMPropertyName.toUtf8().data());
    }
    palette->AddLink(colorName.toUtf8().data(), this->SMProxy, this->SMPropertyName.toUtf8().data(),
      /*unlink_if_modified=*/true);
  }
}

//-----------------------------------------------------------------------------
QString pqColorPaletteLinkHelper::selectedPaletteColor() const
{
  pqColorChooserButtonWithPalettes* button =
    qobject_cast<pqColorChooserButtonWithPalettes*>(this->parent());
  assert(button);

  vtkSMSettingsProxy* palette = button->colorPalette();
  if (palette && this->SMProxy)
  {
    return palette->GetSourcePropertyName(this->SMProxy, this->SMPropertyName.toUtf8().data());
  }

  return QString();
}
