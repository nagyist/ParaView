// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-FileCopyrightText: Copyright (c) Sandia Corporation
// SPDX-License-Identifier: BSD-3-Clause
#include "pqPropertiesPanel.h"
#include "ui_pqPropertiesPanel.h"

#include "pqActiveObjects.h"
#include "pqApplicationCore.h"
#include "pqCoreUtilities.h"
#include "pqDataRepresentation.h"
#include "pqExtractor.h"
#include "pqLiveInsituManager.h"
#include "pqOutputPort.h"
#include "pqPipelineSource.h"
#include "pqProxyWidget.h"
#include "pqScopedOverrideCursor.h"
#include "pqSearchBox.h"
#include "pqServerManagerModel.h"
#include "pqUndoStack.h"
#include "pqWidgetUtilities.h"
#include "vtkCommand.h"
#include "vtkEventQtSlotConnect.h"
#include "vtkNew.h"
#include "vtkPVGeneralSettings.h"
#include "vtkPVLogger.h"
#include "vtkSMProperty.h"
#include "vtkSMProxyClipboard.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMViewProxy.h"
#include "vtkTimerLog.h"

#include <QApplication>
#include <QKeyEvent>
#include <QPointer>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include <cassert>

namespace
{
bool isDeletable(pqProxy* proxy)
{
  if (proxy && pqLiveInsituManager::isInsitu(proxy))
  {
    return false;
  }

  if (auto source = qobject_cast<pqPipelineSource*>(proxy))
  {
    return source->getNumberOfConsumers() == 0;
  }
  return proxy ? true : false;
}

// internal class used to keep track of all the widgets associated with a
// panel either for a source or representation.
class pqProxyWidgets : public QObject
{
  static unsigned long Counter;
  unsigned long MyId;

public:
  typedef QObject Superclass;
  QPointer<pqProxy> Proxy;
  QPointer<pqProxyWidget> Panel;
  pqProxyWidgets(pqProxy* proxy, pqPropertiesPanel* panel)
    : Superclass(panel)
  {
    this->MyId = pqProxyWidgets::Counter++;

    this->Proxy = proxy;
    this->Panel = new pqProxyWidget(proxy->getProxy());
    this->Panel->setObjectName(QString("HiddenProxyPanel%1").arg(this->MyId));
    this->Panel->setView(panel->view());
    QObject::connect(panel, SIGNAL(viewChanged(pqView*)), this->Panel, SLOT(setView(pqView*)));
  }

  ~pqProxyWidgets() override { delete this->Panel; }

  void hide()
  {
    this->Panel->setObjectName(QString("HiddenProxyPanel%1").arg(this->MyId));
    this->Panel->hide();
    this->Panel->parentWidget()->layout()->removeWidget(this->Panel);
  }

  void show(QWidget* parentWdg)
  {
    assert(parentWdg != nullptr);

    delete parentWdg->layout();
    QVBoxLayout* layout = new QVBoxLayout(parentWdg);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    this->Panel->setObjectName("ProxyPanel");
    this->Panel->setParent(parentWdg);
    layout->addWidget(this->Panel);
    this->Panel->show();
  }

  void showWidgets(bool show_advanced, const QString& filterText)
  {
    this->Panel->filterWidgets(show_advanced, filterText);
  }

  void apply(pqView* vtkNotUsed(view))
  {
    vtkVLogIfF(PARAVIEW_LOG_APPLICATION_VERBOSITY(), this->Proxy != nullptr,
      "applying changes to `%s`", this->Proxy->getProxy()->GetLogNameOrDefault());
    this->Panel->apply();
  }

  void reset()
  {
    this->Panel->reset();

    // this ensures that we don't change the state of UNINITIALIZED proxies.
    if (this->Proxy->modifiedState() == pqProxy::MODIFIED)
    {
      this->Proxy->setModifiedState(pqProxy::UNMODIFIED);
    }
  }

private:
  Q_DISABLE_COPY(pqProxyWidgets)
};

unsigned long pqProxyWidgets::Counter = 0;
};

//*****************************************************************************
class pqPropertiesPanel::pqInternals
{
public:
  Ui::propertiesPanel Ui;
  QPointer<pqView> View;
  QPointer<pqProxy> Source;
  int SourcePort;
  QPointer<pqDataRepresentation> Representation;
  QMap<void*, QPointer<pqProxyWidgets>> SourceWidgets;
  QPointer<pqProxyWidgets> DisplayWidgets;
  QPointer<pqProxyWidgets> ViewWidgets;
  bool ReceivedChangeAvailable;
  vtkNew<vtkSMProxyClipboard> SourceClipboard;
  vtkNew<vtkSMProxyClipboard> DisplayClipboard;
  vtkNew<vtkSMProxyClipboard> ViewClipboard;

  vtkNew<vtkEventQtSlotConnect> RepresentationEventConnect;

  //---------------------------------------------------------------------------
  pqInternals(pqPropertiesPanel* panel)
    : SourcePort(-1)
    , ReceivedChangeAvailable(false)
  {
    this->Ui.setupUi(panel);
    pqWidgetUtilities::formatChildTooltips(panel);

    // Setup background color for Apply button so users notice it when it's
    // enabled.

    // if XP Style is being used swap it out for cleanlooks which looks
    // almost the same so we can have a green apply button make all
    // the buttons the same
    QStyle* styleLocal = this->Ui.Accept->style();
    QString styleName = styleLocal->metaObject()->className();
    if (styleName == "QWindowsXPStyle")
    {
      styleLocal = QStyleFactory::create("cleanlooks");
      styleLocal->setParent(panel);
      this->Ui.Accept->setStyle(styleLocal);
      this->Ui.Reset->setStyle(styleLocal);
      this->Ui.Delete->setStyle(styleLocal);
      this->Ui.PropertiesRestoreDefaults->setStyle(styleLocal);
      this->Ui.PropertiesSaveAsDefaults->setStyle(styleLocal);
      this->Ui.DisplayRestoreDefaults->setStyle(styleLocal);
      this->Ui.DisplaySaveAsDefaults->setStyle(styleLocal);
      this->Ui.ViewRestoreDefaults->setStyle(styleLocal);
      this->Ui.DisplaySaveAsDefaults->setStyle(styleLocal);
      QPalette buttonPalette = this->Ui.Accept->palette();
      buttonPalette.setColor(QPalette::Button, QColor(244, 246, 244));
      this->Ui.Accept->setPalette(buttonPalette);
      this->Ui.Reset->setPalette(buttonPalette);
      this->Ui.Delete->setPalette(buttonPalette);
    }

    // Add icons to the settings save defaults buttons
    this->Ui.PropertiesSaveAsDefaults->setIcon(
      styleLocal->standardIcon(QStyle::SP_DialogSaveButton));
    this->Ui.DisplaySaveAsDefaults->setIcon(styleLocal->standardIcon(QStyle::SP_DialogSaveButton));
    this->Ui.ViewSaveAsDefaults->setIcon(styleLocal->standardIcon(QStyle::SP_DialogSaveButton));

    this->Ui.PropertiesButtons->layout()->setSpacing(
      pqPropertiesPanel::suggestedHorizontalSpacing());
    this->Ui.DisplayButtons->layout()->setSpacing(pqPropertiesPanel::suggestedHorizontalSpacing());
    this->Ui.ViewButtons->layout()->setSpacing(pqPropertiesPanel::suggestedHorizontalSpacing());

    // change the apply button palette so it is green when it is enabled.
    pqCoreUtilities::initializeClickMeButton(this->Ui.Accept);

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // Re-apply the palette when the color scheme changes.
    QObject::connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, this->Ui.Accept,
      [button = this->Ui.Accept](Qt::ColorScheme)
      { pqCoreUtilities::initializeClickMeButton(button); });
#endif
  }

  ~pqInternals()
  {
    Q_FOREACH (pqProxyWidgets* widgets, this->SourceWidgets)
    {
      delete widgets;
    }
    this->SourceWidgets.clear();
    delete this->DisplayWidgets;
    delete this->ViewWidgets;
  }

  //---------------------------------------------------------------------------
  void updateInformationAndDomains()
  {
    if (this->Source)
    {
      vtkSMProxy* proxy = this->Source->getProxy();
      if (vtkSMSourceProxy* sourceProxy = vtkSMSourceProxy::SafeDownCast(proxy))
      {
        sourceProxy->UpdatePipelineInformation();
      }
      else
      {
        proxy->UpdatePropertyInformation();
      }
    }
  }
};

//-----------------------------------------------------------------------------
pqPropertiesPanel::pqPropertiesPanel(QWidget* parentObject)
  : Superclass(parentObject)
  , Internals(new pqInternals(this))
  , PanelMode(pqPropertiesPanel::ALL_PROPERTIES)
{
  //---------------------------------------------------------------------------
  // Listen to various signals from the application indicating changes in active
  // source/view/representation, etc.
  pqActiveObjects* activeObjects = &pqActiveObjects::instance();
  this->connect(
    activeObjects, SIGNAL(pipelineProxyChanged(pqProxy*)), this, SLOT(setPipelineProxy(pqProxy*)));
  this->connect(activeObjects, SIGNAL(viewChanged(pqView*)), this, SLOT(setView(pqView*)));
  this->connect(activeObjects, SIGNAL(representationChanged(pqDataRepresentation*)), this,
    SLOT(setRepresentation(pqDataRepresentation*)));

  // listen to server manager changes
  pqServerManagerModel* smm = pqApplicationCore::instance()->getServerManagerModel();
  this->connect(smm, SIGNAL(proxyRemoved(pqProxy*)), SLOT(proxyDeleted(pqProxy*)));
  // this connection ensures that the button state is updated everytime any
  // item's state changes.
  this->connect(
    smm, SIGNAL(modifiedStateChanged(pqServerManagerModelItem*)), SLOT(updateButtonState()));
  this->connect(pqApplicationCore::instance()->getServerManagerModel(),
    SIGNAL(connectionRemoved(pqPipelineSource*, pqPipelineSource*, int)),
    SLOT(updateButtonState()));
  this->connect(pqApplicationCore::instance()->getServerManagerModel(),
    SIGNAL(connectionAdded(pqPipelineSource*, pqPipelineSource*, int)), SLOT(updateButtonState()));

  // Listen to UI signals.
  QObject::connect(this->Internals->Ui.Accept, SIGNAL(clicked()), this, SLOT(apply()));
  QObject::connect(this->Internals->Ui.Reset, SIGNAL(clicked()), this, SLOT(reset()));
  QObject::connect(this->Internals->Ui.Delete, SIGNAL(clicked()), this, SLOT(deleteProxy()));
  QObject::connect(this->Internals->Ui.Help, SIGNAL(clicked()), this, SLOT(showHelp()));
  QObject::connect(
    this->Internals->Ui.SearchBox, SIGNAL(textChanged(QString)), this, SLOT(updatePanel()));
  QObject::connect(this->Internals->Ui.PropertiesRestoreDefaults, SIGNAL(clicked()), this,
    SLOT(propertiesRestoreDefaults()));
  QObject::connect(this->Internals->Ui.PropertiesSaveAsDefaults, SIGNAL(clicked()), this,
    SLOT(propertiesSaveAsDefaults()));
  QObject::connect(this->Internals->Ui.SearchBox, SIGNAL(advancedSearchActivated(bool)), this,
    SLOT(updatePanel()));

  QObject::connect(this->Internals->Ui.PropertiesButton, SIGNAL(toggled(bool)),
    this->Internals->Ui.PropertiesFrame, SLOT(setVisible(bool)));
  QObject::connect(this->Internals->Ui.DisplayRestoreDefaults, SIGNAL(clicked()), this,
    SLOT(displayRestoreDefaults()));
  QObject::connect(this->Internals->Ui.DisplaySaveAsDefaults, SIGNAL(clicked()), this,
    SLOT(displaySaveAsDefaults()));
  QObject::connect(
    this->Internals->Ui.ViewRestoreDefaults, SIGNAL(clicked()), this, SLOT(viewRestoreDefaults()));
  QObject::connect(
    this->Internals->Ui.ViewSaveAsDefaults, SIGNAL(clicked()), this, SLOT(viewSaveAsDefaults()));
  QObject::connect(this->Internals->Ui.DisplayButton, SIGNAL(toggled(bool)),
    this->Internals->Ui.DisplayFrame, SLOT(setVisible(bool)));
  QObject::connect(this->Internals->Ui.ViewButton, SIGNAL(toggled(bool)),
    this->Internals->Ui.ViewFrame, SLOT(setVisible(bool)));

  this->connect(this->Internals->Ui.PropertiesCopy, SIGNAL(clicked()), SLOT(copyProperties()));
  this->connect(this->Internals->Ui.PropertiesPaste, SIGNAL(clicked()), SLOT(pasteProperties()));
  this->connect(this->Internals->Ui.DisplayCopy, SIGNAL(clicked()), SLOT(copyDisplay()));
  this->connect(this->Internals->Ui.DisplayPaste, SIGNAL(clicked()), SLOT(pasteDisplay()));
  this->connect(this->Internals->Ui.ViewCopy, SIGNAL(clicked()), SLOT(copyView()));
  this->connect(this->Internals->Ui.ViewPaste, SIGNAL(clicked()), SLOT(pasteView()));

  this->setPipelineProxy(nullptr);
  this->setView(nullptr);
  this->setRepresentation(nullptr);
}

//-----------------------------------------------------------------------------
pqPropertiesPanel::~pqPropertiesPanel()
{
  delete this->Internals;
  this->Internals = nullptr;
}

//-----------------------------------------------------------------------------
bool pqPropertiesPanel::canApply()
{
  Ui::propertiesPanel& ui = this->Internals->Ui;
  return ui.Accept->isEnabled();
}

//-----------------------------------------------------------------------------
bool pqPropertiesPanel::canReset()
{
  Ui::propertiesPanel& ui = this->Internals->Ui;
  return ui.Reset->isEnabled();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::setPanelMode(int val)
{
  if (this->PanelMode == val)
  {
    return;
  }

  this->PanelMode = val;

  // show buttons only when showing source properties.
  bool has_source = (this->PanelMode & pqPropertiesPanel::SOURCE_PROPERTIES) != 0;
  bool has_display = (this->PanelMode & pqPropertiesPanel::DISPLAY_PROPERTIES) != 0;
  bool has_view = (this->PanelMode & pqPropertiesPanel::VIEW_PROPERTIES) != 0;

  this->Internals->Ui.Accept->setVisible(has_source);
  this->Internals->Ui.Delete->setVisible(has_source);
  this->Internals->Ui.Help->setVisible(has_source);
  this->Internals->Ui.Reset->setVisible(has_source);

  this->Internals->Ui.PropertiesButtons->setVisible(has_source);
  this->Internals->Ui.PropertiesFrame->setVisible(has_source);

  this->Internals->Ui.ViewFrame->setVisible(has_view);
  this->Internals->Ui.ViewButtons->setVisible(has_view);

  this->Internals->Ui.DisplayFrame->setVisible(has_display);
  this->Internals->Ui.DisplayButtons->setVisible(has_display);

  // the buttons need not be shown if there's only 1 type in the panel.
  bool has_multiples_types =
    ((has_source ? 1 : 0) + (has_display ? 1 : 0) + (has_view ? 1 : 0)) > 1;
  this->Internals->Ui.PropertiesButton->setVisible(has_multiples_types && has_source);
  this->Internals->Ui.DisplayButton->setVisible(has_multiples_types && has_display);
  this->Internals->Ui.ViewButton->setVisible(has_multiples_types && has_view);

  this->updatePanel();
}

//-----------------------------------------------------------------------------
pqView* pqPropertiesPanel::view() const
{
  return this->Internals->View;
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::setRepresentation(pqDataRepresentation* repr)
{
  this->updateDisplayPanel(repr);
  this->updateButtonState();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::setView(pqView* pqview)
{
  this->updateViewPanel(pqview);
  this->updateButtonState();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::setPipelineProxy(pqProxy* proxy)
{
  if (auto port = qobject_cast<pqOutputPort*>(proxy))
  {
    this->Internals->SourcePort = port->getPortNumber();
    proxy = port->getSource();
  }
  else if (proxy)
  {
    this->Internals->SourcePort = 0;
  }
  else
  {
    this->Internals->SourcePort = -1;
  }

  this->updatePropertiesPanel(proxy);
  this->updateButtonState();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::updatePanel()
{
  auto& internals = (*this->Internals);
  if (!internals.Source.isNull() && internals.SourcePort < 0)
  {
    internals.SourcePort = 0;
  }

  this->updatePropertiesPanel(internals.Source);
  this->updateDisplayPanel(internals.Representation);
  this->updateViewPanel(internals.View);
  this->updateButtonState();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::updatePropertiesPanel(pqProxy* source)
{
  if ((this->PanelMode & SOURCE_PROPERTIES) == 0)
  {
    source = nullptr;
  }

  bool created = false;
  if (this->Internals->Source != source)
  {
    // Panel has changed.
    if (this->Internals->Source && this->Internals->SourceWidgets.contains(this->Internals->Source))
    {
      this->Internals->SourceWidgets[this->Internals->Source]->hide();
    }
    this->Internals->Source = source;
    this->Internals->updateInformationAndDomains();

    if (source && !this->Internals->SourceWidgets.contains(source))
    {
      // create the panel for the source.
      created = true;
      pqProxyWidgets* widgets = new pqProxyWidgets(source, this);
      this->Internals->SourceWidgets[source] = widgets;

      QObject::connect(
        widgets->Panel, SIGNAL(changeAvailable()), this, SLOT(sourcePropertyChangeAvailable()));
      QObject::connect(
        widgets->Panel, SIGNAL(changeFinished()), this, SLOT(sourcePropertyChanged()));
    }

    if (source)
    {
      this->Internals->SourceWidgets[source]->show(this->Internals->Ui.PropertiesFrame);
    }
  }

  // update widgets.
  if (source)
  {
    auto sourceWidgets = this->Internals->SourceWidgets[source];

    this->Internals->Ui.PropertiesButton->setText(tr("Properties") +
      QString(" ('%1' / %2)")
        .arg(source->getSMName())
        .arg(QCoreApplication::translate("ServerManagerXML", source->getProxy()->GetXMLLabel())));
    sourceWidgets->showWidgets(this->Internals->Ui.SearchBox->isAdvancedSearchActive(),
      this->Internals->Ui.SearchBox->text());

    // update interactive widgets specifically
    if (this->Internals->SourcePort >= 0)
    {
      // change the focus for newly created pqProxyWidgets
      sourceWidgets->Panel->showLinkedInteractiveWidget(this->Internals->SourcePort, true, created);
    }

    Q_EMIT this->modified();
  }
  else
  {
    this->Internals->Ui.PropertiesButton->setText(tr("Properties"));
  }
  this->updateButtonEnableState();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::updateDisplayPanel()
{
  this->updateDisplayPanel(this->Internals->Representation);
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::updateDisplayPanel(pqDataRepresentation* repr)
{
  if ((this->PanelMode & pqPropertiesPanel::DISPLAY_PROPERTIES) == 0)
  {
    repr = nullptr;
  }

  // since this->Internals->Representation is QPointer, it can go nullptr (e.g. during
  // disconnect) before we get the chance to clear the panel's widgets. Hence we
  // do the block of code if (repr==nullptr) event if nothing has changed.
  if (this->Internals->Representation != repr || repr == nullptr)
  {
    // Representation has changed, destroy the current display panel and create
    // a new one. Unlike properties panels, display panels are not cached.
    if (this->Internals->DisplayWidgets)
    {
      this->Internals->DisplayWidgets->hide();
      delete this->Internals->DisplayWidgets;
    }
    this->Internals->RepresentationEventConnect->Disconnect();
    this->Internals->Representation = repr;
    if (repr)
    {
      // create the panel for the repr.
      pqProxyWidgets* widgets = new pqProxyWidgets(repr, this);
      widgets->Panel->setApplyChangesImmediately(true);
      QObject::connect(widgets->Panel, SIGNAL(changeFinished()), this, SLOT(renderActiveView()));
      this->Internals->DisplayWidgets = widgets;
      this->Internals->DisplayWidgets->show(this->Internals->Ui.DisplayFrame);

      if (repr->getProxy()->GetProperty("Representation"))
      {
        this->Internals->RepresentationEventConnect->Connect(
          repr->getProxy()->GetProperty("Representation"), vtkCommand::ModifiedEvent, this,
          SLOT(updateDisplayPanel()));
      }
    }
  }

  if (repr)
  {
    this->Internals->Ui.DisplayButton->setText(
      tr("Display") + QString(" (%1)").arg(repr->getProxy()->GetXMLName()));
    this->Internals->DisplayWidgets->showWidgets(
      this->Internals->Ui.SearchBox->isAdvancedSearchActive(),
      this->Internals->Ui.SearchBox->text());
  }
  else
  {
    this->Internals->Ui.DisplayButton->setText(tr("Display"));
  }

  this->updateButtonEnableState();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::updateViewPanel(pqView* argView)
{
  pqView* _view = argView;
  if ((this->PanelMode & pqPropertiesPanel::VIEW_PROPERTIES) == 0)
  {
    _view = nullptr;
  }

  if (this->Internals->View != argView)
  {
    // The view has changed.
    if (this->Internals->View)
    {
      if (this->Internals->ViewWidgets)
      {
        this->Internals->ViewWidgets->hide();
        delete this->Internals->ViewWidgets;
      }
    }
    this->Internals->View = argView;
    Q_EMIT this->viewChanged(argView);
    if (_view)
    {
      // create the widgets for this view
      pqProxyWidgets* widgets = new pqProxyWidgets(_view, this);
      widgets->Panel->setApplyChangesImmediately(true);
      QObject::connect(widgets->Panel, SIGNAL(changeFinished()), this, SLOT(renderActiveView()));
      this->Internals->ViewWidgets = widgets;
      this->Internals->ViewWidgets->show(this->Internals->Ui.ViewFrame);
    }
  }

  if (_view)
  {
    // update the label and show the widgets
    vtkSMViewProxy* proxy = _view->getViewProxy();
    const char* label = proxy->GetXMLLabel();
    this->Internals->Ui.ViewButton->setText(
      tr("View (%1)")
        .arg(label != nullptr ? QCoreApplication::translate("ServerManagerXML", label)
                              : _view->getViewType()));
    this->Internals->ViewWidgets->showWidgets(
      this->Internals->Ui.SearchBox->isAdvancedSearchActive(),
      this->Internals->Ui.SearchBox->text());
  }
  else
  {
    this->Internals->Ui.ViewButton->setText(tr("View"));
  }

  this->updateButtonEnableState();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::renderActiveView()
{
  if (this->view())
  {
    this->view()->render();
  }
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::sourcePropertyChanged(bool change_finished /*=true*/)
{
  std::string proxyLabel("(unknown)");
  if (auto signalSender = qobject_cast<pqProxyWidget*>(this->sender()))
  {
    if (auto proxy = signalSender->proxy())
    {
      proxyLabel = proxy->GetLogNameOrDefault();
    }
  }

  if (!change_finished)
  {
    this->Internals->ReceivedChangeAvailable = true;
  }
  if (change_finished && !this->Internals->ReceivedChangeAvailable)
  {
    vtkVLogF(PARAVIEW_LOG_APPLICATION_VERBOSITY(),
      "received `changeFinished` signal without receiving a `changeAvailable` "
      "signal from `%s`'s proxy-widget; ignoring it!",
      proxyLabel.c_str());
    return;
  }

  vtkVLogF(PARAVIEW_LOG_APPLICATION_VERBOSITY(), "received `%s` from `%s`'s proxy-widget",
    (change_finished ? "changeFinished" : "changeAvailable"), proxyLabel.c_str());
  if (this->Internals->Source && this->Internals->Source->modifiedState() == pqProxy::UNMODIFIED)
  {
    this->Internals->Source->setModifiedState(pqProxy::MODIFIED);
  }
  if (change_finished)
  {
    Q_EMIT this->modified();
  }
  this->updateButtonState();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::updateButtonState()
{
  const Ui::propertiesPanel& ui = this->Internals->Ui;

  const bool previous_apply_state = ui.Accept->isEnabled();

  ui.Accept->setEnabled(false);
  ui.Reset->setEnabled(false);
  ui.Help->setEnabled(this->Internals->Source != nullptr);
  ui.Delete->setEnabled(isDeletable(this->Internals->Source));

  Q_FOREACH (const pqProxyWidgets* widgets, this->Internals->SourceWidgets)
  {
    pqProxy* proxy = widgets->Proxy;
    if (proxy == nullptr)
    {
      continue;
    }

    ui.PropertiesRestoreDefaults->setEnabled(true);
    if (proxy->modifiedState() == pqProxy::UNINITIALIZED)
    {
      vtkVLogIfF(PARAVIEW_LOG_APPLICATION_VERBOSITY(), previous_apply_state == false,
        "`Apply` button enabled since `%s` became uninitialized.",
        proxy->getProxy()->GetLogNameOrDefault());
      ui.Accept->setEnabled(true);
      ui.PropertiesSaveAsDefaults->setEnabled(true);
    }
    else if (proxy->modifiedState() == pqProxy::MODIFIED)
    {
      vtkVLogIfF(PARAVIEW_LOG_APPLICATION_VERBOSITY(), previous_apply_state == false,
        "`Apply` button enabled since `%s` became modified.",
        proxy->getProxy()->GetLogNameOrDefault());
      ui.Accept->setEnabled(true);
      ui.Reset->setEnabled(true);
      ui.PropertiesSaveAsDefaults->setEnabled(true);
    }
  }

  vtkVLogIfF(PARAVIEW_LOG_APPLICATION_VERBOSITY(),
    (previous_apply_state && !ui.Accept->isEnabled()),
    "`Apply` button disabled since no changes are apply-able changes are present.");

  if (!ui.Accept->isEnabled())
  {
    // It's a good place to reset the ReceivedChangeAvailable if Accept button
    // is not enabled. This is same as doing it in apply()/reset() or if the
    // only modified proxy is deleted.
    this->Internals->ReceivedChangeAvailable = false;
  }

  Q_EMIT this->applyEnableStateChanged();
  this->updateButtonEnableState();
}

//-----------------------------------------------------------------------------
/// Updates enabled state for buttons on panel (other than
/// apply/reset/delete);
void pqPropertiesPanel::updateButtonEnableState()
{
  pqInternals& internals = *this->Internals;
  Ui::propertiesPanel& ui = internals.Ui;

  // Update PropertiesSaveAsDefaults and PropertiesRestoreDefaults state.
  // If the source's properties are yet to be applied, we disable the two
  // buttons (see BUG #15338).
  bool canSaveSourcePropertyDefaults = internals.Source != nullptr
    ? (internals.Source->modifiedState() == pqProxy::UNMODIFIED)
    : false;
  ui.PropertiesSaveAsDefaults->setEnabled(canSaveSourcePropertyDefaults);

  ui.DisplayRestoreDefaults->setEnabled(internals.DisplayWidgets != nullptr);
  ui.DisplaySaveAsDefaults->setEnabled(internals.DisplayWidgets != nullptr);

  ui.ViewRestoreDefaults->setEnabled(internals.ViewWidgets != nullptr);
  ui.ViewSaveAsDefaults->setEnabled(internals.ViewWidgets != nullptr);

  // Now update copy-paste button state as well.
  if (internals.Source)
  {
    ui.PropertiesCopy->setEnabled(true);
    ui.PropertiesPaste->setEnabled(
      internals.SourceClipboard->CanPaste(internals.Source->getProxy()));
  }
  else
  {
    ui.PropertiesCopy->setEnabled(false);
    ui.PropertiesPaste->setEnabled(false);
  }
  if (internals.Representation)
  {
    ui.DisplayCopy->setEnabled(true);
    ui.DisplayPaste->setEnabled(
      internals.DisplayClipboard->CanPaste(internals.Representation->getProxy()));
  }
  else
  {
    ui.DisplayCopy->setEnabled(false);
    ui.DisplayPaste->setEnabled(false);
  }
  if (internals.View)
  {
    ui.ViewCopy->setEnabled(true);
    ui.ViewPaste->setEnabled(internals.ViewClipboard->CanPaste(internals.View->getProxy()));
  }
  else
  {
    ui.ViewCopy->setEnabled(false);
    ui.ViewPaste->setEnabled(false);
  }
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::apply()
{
  vtkVLogScopeFunction(PARAVIEW_LOG_APPLICATION_VERBOSITY());

  vtkTimerLog::MarkStartEvent("PropertiesPanel::Apply");

  BEGIN_UNDO_SET(tr("Apply"));

  pqScopedOverrideCursor scopedWaitCursor(Qt::WaitCursor);

  bool onlyApplyCurrentPanel = vtkPVGeneralSettings::GetInstance()->GetAutoApplyActiveOnly();

  if (onlyApplyCurrentPanel)
  {
    pqProxyWidgets* widgets =
      this->Internals->Source ? this->Internals->SourceWidgets[this->Internals->Source] : nullptr;
    if (widgets)
    {
      widgets->apply(this->view());
      Q_EMIT this->applied(widgets->Proxy);
    }
  }
  else
  {
    Q_FOREACH (pqProxyWidgets* widgets, this->Internals->SourceWidgets)
    {
      widgets->apply(this->view());
      Q_EMIT this->applied(widgets->Proxy);
    }
  }

  this->Internals->updateInformationAndDomains();
  this->updateButtonState();

  Q_EMIT this->applied();
  END_UNDO_SET();
  vtkTimerLog::MarkEndEvent("PropertiesPanel::Apply");
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::reset()
{
  bool onlyApplyCurrentPanel = vtkPVGeneralSettings::GetInstance()->GetAutoApplyActiveOnly();
  if (onlyApplyCurrentPanel)
  {
    pqProxyWidgets* widgets =
      this->Internals->Source ? this->Internals->SourceWidgets[this->Internals->Source] : nullptr;
    if (widgets)
    {
      widgets->reset();
    }
  }
  else
  {
    Q_FOREACH (pqProxyWidgets* widgets, this->Internals->SourceWidgets)
    {
      widgets->reset();
    }
  }
  Q_EMIT this->resetDone();

  this->Internals->updateInformationAndDomains();
  this->updateButtonState();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::deleteProxy()
{
  if (auto source = this->Internals->Source.data())
  {
    BEGIN_UNDO_SET(tr("Delete") + " " + source->getSMName());
    Q_EMIT this->deleteRequested(source);
    END_UNDO_SET();
  }
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::showHelp()
{
  if (this->Internals->Source)
  {
    this->helpRequested(this->Internals->Source->getProxy()->GetXMLGroup(),
      this->Internals->Source->getProxy()->GetXMLName());
  }
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::propertiesRestoreDefaults()
{
  pqProxyWidgets* widgets =
    this->Internals->Source ? this->Internals->SourceWidgets[this->Internals->Source] : nullptr;
  if (widgets && widgets->Panel)
  {
    widgets->Panel->restoreDefaults();
  }
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::propertiesSaveAsDefaults()
{
  pqProxyWidgets* widgets =
    this->Internals->Source ? this->Internals->SourceWidgets[this->Internals->Source] : nullptr;
  if (widgets && widgets->Panel)
  {
    widgets->Panel->saveAsDefaults();
  }
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::displayRestoreDefaults()
{
  if (this->Internals->DisplayWidgets)
  {
    if (this->Internals->DisplayWidgets->Panel->restoreDefaults())
    {
      this->Internals->DisplayWidgets->Panel->apply();
    }
  }
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::displaySaveAsDefaults()
{
  if (this->Internals->DisplayWidgets)
  {
    this->Internals->DisplayWidgets->Panel->saveAsDefaults();
  }
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::viewRestoreDefaults()
{
  if (this->Internals->ViewWidgets)
  {
    if (this->Internals->ViewWidgets->Panel->restoreDefaults())
    {
      this->Internals->ViewWidgets->Panel->apply();
    }
  }
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::viewSaveAsDefaults()
{
  if (this->Internals->ViewWidgets)
  {
    this->Internals->ViewWidgets->Panel->saveAsDefaults();
  }
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::proxyDeleted(pqProxy* source)
{
  if (this->Internals->Source == source)
  {
    this->setPipelineProxy(nullptr);
  }
  if (this->Internals->SourceWidgets.contains(source))
  {
    delete this->Internals->SourceWidgets[source];
    this->Internals->SourceWidgets.remove(source);
  }

  this->updateButtonState();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::copyProperties()
{
  this->Internals->SourceClipboard->Copy(this->Internals->Source->getProxy());
  this->updateButtonEnableState();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::pasteProperties()
{
  this->Internals->SourceClipboard->Paste(this->Internals->Source->getProxy());
  this->renderActiveView();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::copyDisplay()
{
  this->Internals->DisplayClipboard->Copy(this->Internals->Representation->getProxy());
  this->updateButtonEnableState();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::pasteDisplay()
{
  this->Internals->DisplayClipboard->Paste(this->Internals->Representation->getProxy());
  this->renderActiveView();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::copyView()
{
  this->Internals->ViewClipboard->Copy(this->Internals->View->getProxy());
  this->updateButtonEnableState();
}

//-----------------------------------------------------------------------------
void pqPropertiesPanel::pasteView()
{
  this->Internals->ViewClipboard->Paste(this->Internals->View->getProxy());
  this->renderActiveView();
}
