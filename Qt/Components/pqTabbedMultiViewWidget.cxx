// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-FileCopyrightText: Copyright (c) Sandia Corporation
// SPDX-License-Identifier: BSD-3-Clause
#include "pqTabbedMultiViewWidget.h"

#include "pqActiveObjects.h"
#include "pqApplicationCore.h"
#include "pqMultiViewWidget.h"
#include "pqObjectBuilder.h"
#include "pqServer.h"
#include "pqServerManagerModel.h"
#include "pqServerManagerObserver.h"
#include "pqUndoStack.h"
#include "pqView.h"
#include "pqWidgetUtilities.h"
#include "vtkCommand.h"
#include "vtkErrorCode.h"
#include "vtkImageData.h"
#include "vtkNew.h"
#include "vtkSMParaViewPipelineControllerWithRendering.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxyManager.h"
#include "vtkSMSaveScreenshotProxy.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMTrace.h"
#include "vtkSMUtilities.h"
#include "vtkSMViewLayoutProxy.h"

#include "pqQVTKWidget.h"
#include "pqViewFrame.h"

#include <QEvent>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMap>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QShortcut>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QtDebug>

#include <cassert>

static const int PQTABBED_WIDGET_PIXMAP_SIZE = 16;
//-----------------------------------------------------------------------------
// ******************** pqTabWidget **********************
//-----------------------------------------------------------------------------
pqTabbedMultiViewWidget::pqTabWidget::pqTabWidget(QWidget* parentObject)
  : Superclass(parentObject)
  , ReadOnly(false)
  , TabBarVisibility(true)
{
  // If annotation filters are enabled, it is possible to get into a situation
  // where no layouts are visible. This means the new-layout tab ("+") is both
  // visible and active – users cannot switch to it to create a new layout with
  // a view-selector because it is already active. In that case, the connection
  // below allows us to explicitly create a new layout.
  QObject::connect(
    this, &QTabWidget::tabBarClicked, this, &pqTabWidget::createViewSelectorTabIfNeeded);
}

//-----------------------------------------------------------------------------
pqTabbedMultiViewWidget::pqTabWidget::~pqTabWidget() = default;

//-----------------------------------------------------------------------------
int pqTabbedMultiViewWidget::pqTabWidget::tabButtonIndex(
  QWidget* wdg, QTabBar::ButtonPosition position) const
{
  for (int cc = 0; cc < this->count(); cc++)
  {
    if (this->tabBar()->tabButton(cc, position) == wdg)
    {
      return cc;
    }
  }
  return -1;
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::pqTabWidget::setTabButton(
  int index, QTabBar::ButtonPosition position, QWidget* wdg)
{
  this->tabBar()->setTabButton(index, position, wdg);
}

//-----------------------------------------------------------------------------
int pqTabbedMultiViewWidget::pqTabWidget::addAsTab(
  pqMultiViewWidget* wdg, pqTabbedMultiViewWidget* self)
{
  int tab_count = this->count();
  if (this->ReadOnly)
  {
    // Add one for the missing '+' button which is not present in ReadOnly mode.
    // The logic later on assumes an additional tab.
    tab_count++;
  }
  vtkSMViewLayoutProxy* proxy = wdg->layoutManager();
  pqServerManagerModel* smmodel = pqApplicationCore::instance()->getServerManagerModel();
  pqProxy* item = smmodel->findItem<pqProxy*>(proxy);
  int tab_index = this->insertTab(tab_count - 1, wdg, item->getSMName());
  this->connect(item, SIGNAL(nameChanged(pqServerManagerModelItem*)), self,
    SLOT(onLayoutNameChanged(pqServerManagerModelItem*)));

  QLabel* label = new QLabel();
  label->setObjectName("popout");
  label->setToolTip(pqWidgetUtilities::formatTooltip(pqTabWidget::popoutLabelText(false)));
  label->setStatusTip(pqTabWidget::popoutLabelText(false));
  label->setPixmap(label->style()
                     ->standardIcon(pqTabWidget::popoutLabelPixmap(false))
                     .pixmap(PQTABBED_WIDGET_PIXMAP_SIZE, PQTABBED_WIDGET_PIXMAP_SIZE));
  this->setTabButton(tab_index, QTabBar::LeftSide, label);
  label->installEventFilter(self);

  label = new QLabel();
  label->setObjectName("close");
  label->setToolTip(pqWidgetUtilities::formatTooltip(tr("Close layout")));
  label->setStatusTip(tr("Close layout"));
  label->setPixmap(label->style()
                     ->standardIcon(QStyle::SP_TitleBarCloseButton)
                     .pixmap(PQTABBED_WIDGET_PIXMAP_SIZE, PQTABBED_WIDGET_PIXMAP_SIZE));
  this->setTabButton(tab_index, QTabBar::RightSide, label);
  label->installEventFilter(self);
  label->setVisible(!this->ReadOnly);
  return tab_index;
}

//-----------------------------------------------------------------------------
QString pqTabbedMultiViewWidget::pqTabWidget::popoutLabelText(bool popped_out)
{
  return popped_out ? tr("Bring popped out window back to the frame")
                    : tr("Pop out layout in separate window");
}

//-----------------------------------------------------------------------------
QStyle::StandardPixmap pqTabbedMultiViewWidget::pqTabWidget::popoutLabelPixmap(bool popped_out)
{
  return popped_out ? QStyle::SP_TitleBarNormalButton : QStyle::SP_TitleBarMaxButton;
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::pqTabWidget::setReadOnly(bool val)
{
  if (this->ReadOnly == val)
  {
    return;
  }

  this->ReadOnly = val;
  QList<QLabel*> labels = this->findChildren<QLabel*>("close");
  Q_FOREACH (QLabel* label, labels)
  {
    label->setVisible(!val);
  }
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::pqTabWidget::setTabBarVisibility(bool val)
{
  this->TabBarVisibility = val;
  this->tabBar()->setVisible(val);
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::pqTabWidget::createViewSelectorTabIfNeeded(int tabIndex)
{
  (void)tabIndex;
  if (this->currentIndex() == 0 && this->count() == 1)
  {
    pqServer* server = pqActiveObjects::instance().activeServer();
    auto* tmv = qobject_cast<pqTabbedMultiViewWidget*>(this->parent());
    if (tmv)
    {
      tmv->createTab(server);
    }
  }
}

//-----------------------------------------------------------------------------
QSize pqTabbedMultiViewWidget::pqTabWidget::preview(const QSize& nsize)
{
  // NOTE: do for all widgets? Currently, we'll only do this for the current
  // widget.
  if (pqMultiViewWidget* widget = qobject_cast<pqMultiViewWidget*>(this->currentWidget()))
  {
    return widget->preview(nsize);
  }
  return QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
}

//-----------------------------------------------------------------------------
// ****************     pqTabbedMultiViewWidget   **********************
//-----------------------------------------------------------------------------
class pqTabbedMultiViewWidget::pqInternals
{
  bool DecorationsVisibility = true;

  // keeps tracks of pqMultiViewWidget instances.
  QMap<pqServer*, QList<QPointer<pqMultiViewWidget>>> TabWidgets;

  QString FilterAnnotationKey;
  bool FilterAnnotationMatching = true;

public:
  QPointer<pqTabWidget> TabWidget;

  QPointer<QWidget> FullScreenWindow;
  QPointer<QWidget> NewTabWidget;
  QPointer<QFrame> ActiveFrame;

  void addNewTabWidget()
  {
    if (!this->NewTabWidget)
    {
      this->NewTabWidget = new QWidget(this->TabWidget);
      this->TabWidget->addTab(this->NewTabWidget, "+");
    }
  }
  void removeNewTabWidget()
  {
    if (this->NewTabWidget)
    {
      this->TabWidget->removeTab(this->TabWidget->indexOf(this->NewTabWidget));
      delete this->NewTabWidget;
    }
  }

  void setDecorationsVisibility(bool val)
  {
    this->TabWidget->setTabBarVisibility(val);
    this->DecorationsVisibility = val;
    for (int cc = 0, max = this->TabWidget->count(); cc < max; ++cc)
    {
      if (auto mvwidget = qobject_cast<pqMultiViewWidget*>(this->TabWidget->widget(cc)))
      {
        mvwidget->setDecorationsVisibility(val);
      }
    }
  }

  bool decorationsVisibility() const { return this->DecorationsVisibility; }

  /// adds a vtkSMViewLayoutProxy to a new tab.
  int addTab(vtkSMViewLayoutProxy* vlayout, pqTabbedMultiViewWidget* self)
  {
    const int count = this->TabWidget->count();
    auto widget = new pqMultiViewWidget();
    widget->setObjectName(QString("MultiViewWidget%1").arg(count));
    widget->setLayoutManager(vlayout);
    widget->setDecorationsVisibility(this->decorationsVisibility());

    // ensure that the tab current when the pqMultiViewWidget becomes
    // active.
    QObject::connect(widget, &pqMultiViewWidget::frameActivated,
      [this, widget]() { this->TabWidget->setCurrentWidget(widget); });

    auto server =
      pqApplicationCore::instance()->getServerManagerModel()->findServer(vlayout->GetSession());

    this->TabWidgets[server].push_back(widget);

    const int tab_index = this->isVisible(vlayout) ? this->TabWidget->addAsTab(widget, self) : -1;
    return tab_index;
  }

  /**
   * this will return -1 if vlayout is hidden.
   */
  int tabIndex(vtkSMProxy* vlayout) const
  {
    if (auto widget = this->findWidget(vlayout))
    {
      return this->TabWidget->indexOf(widget);
    }
    return -1;
  }

  void setCurrentTab(int index)
  {
    if (index >= 0 && index < this->TabWidget->count())
    {
      this->TabWidget->setCurrentIndex(index);
    }
  }

  /**
   * Returns pqMultiViewWidget instance given a vlayout, if any. Else
   * nullptr.
   */
  pqMultiViewWidget* findWidget(vtkSMProxy* vlayout) const
  {
    for (const auto& pqmvwidget : this->widgets())
    {
      if (pqmvwidget && pqmvwidget->layoutManager() == vlayout)
      {
        return pqmvwidget;
      }
    }
    return nullptr;
  }

  void removeTab(vtkSMProxy* vlayout)
  {
    auto widget = this->findWidget(vlayout);
    if (widget == nullptr)
    {
      return;
    }

    const auto index = this->TabWidget->indexOf(widget);
    if (index != -1)
    {
      if (index == this->TabWidget->currentIndex())
      {
        this->TabWidget->setCurrentIndex((index - 1) > 0 ? (index - 1) : 0);
      }
      this->TabWidget->removeTab(index);
    }

    auto server =
      pqApplicationCore::instance()->getServerManagerModel()->findServer(vlayout->GetSession());
    this->TabWidgets[server].removeOne(widget);
    delete widget;
  }

  // removes all tabs associated with a server.
  void removeTabs(pqServer* server)
  {
    auto wdgs = this->TabWidgets[server];
    for (const auto& widget : wdgs)
    {
      if (widget)
      {
        this->removeTab(widget->layoutManager());
      }
    }
    this->TabWidgets.remove(server);
  }

  QList<QPointer<pqMultiViewWidget>> widgets() const
  {
    QList<QPointer<pqMultiViewWidget>> wgs;
    for (auto& list : this->TabWidgets)
    {
      wgs += list;
    }
    return wgs;
  }

  bool isVisible(vtkSMViewLayoutProxy* vlayout) const
  {
    Q_ASSERT(vlayout != nullptr);
    if (this->FilterAnnotationKey.isEmpty())
    {
      return true;
    }
    else
    {
      const bool hasAnnotation = vlayout->HasAnnotation(this->FilterAnnotationKey.toUtf8().data());
      return this->FilterAnnotationMatching ? hasAnnotation : !hasAnnotation;
    }
  }

  void updateVisibleTabs()
  {
    // build a list of visible tabs and if they are different,
    // than what's shown, update the view.
    QList<QWidget*> visibleWidgets;
    for (const auto& widget : this->widgets())
    {
      if (widget &&
        (this->isVisible(widget->layoutManager()) || widget->layoutManager()->GetViews().empty()))
      {
        visibleWidgets.push_back(widget);
      }
    }

    if (this->NewTabWidget)
    {
      visibleWidgets.push_back(this->NewTabWidget);
    }

    QList<QWidget*> tabs;
    for (int cc = 0, max = this->TabWidget->count(); cc < max; ++cc)
    {
      tabs.push_back(this->TabWidget->widget(cc));
    }

    if (tabs != visibleWidgets)
    {
      this->TabWidget->clear();
      for (auto wdg : visibleWidgets)
      {
        this->TabWidget->addTab(wdg, this->tabLabel(wdg));
      }
    }
  }

  QString tabLabel(QWidget* wdg) const
  {
    if (wdg == this->NewTabWidget)
    {
      return "+";
    }
    else if (auto mvwidget = qobject_cast<pqMultiViewWidget*>(wdg))
    {
      auto vlayout = mvwidget->layoutManager();
      auto pxm = vlayout->GetSessionProxyManager();
      return pxm->GetProxyName("layouts", vlayout);
    }
    return "?";
  }

  void enableAnnotationFilter(const QString& annotationKey)
  {
    if (this->FilterAnnotationKey != annotationKey)
    {
      this->FilterAnnotationKey = annotationKey;
      this->updateVisibleTabs();
    }
  }

  void disableAnnotationFilter()
  {
    if (!this->FilterAnnotationKey.isEmpty())
    {
      this->FilterAnnotationKey.clear();
      this->updateVisibleTabs();
    }
  }

  void setAnnotationFilterMatching(bool matching)
  {
    if (this->FilterAnnotationMatching != matching)
    {
      this->FilterAnnotationMatching = matching;
      if (!this->FilterAnnotationKey.isEmpty())
      {
        this->updateVisibleTabs();
      }
    }
  }
};

//-----------------------------------------------------------------------------
pqTabbedMultiViewWidget::pqTabbedMultiViewWidget(QWidget* parentObject)
  : Superclass(parentObject)
  , Internals(new pqInternals())
{
  this->Internals->TabWidget = new pqTabWidget(this);
  this->Internals->TabWidget->setObjectName("CoreWidget");

  this->Internals->TabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this->Internals->TabWidget->tabBar(), SIGNAL(customContextMenuRequested(const QPoint&)),
    this, SLOT(contextMenuRequested(const QPoint&)));

  QGridLayout* glayout = new QGridLayout(this);
  glayout->setContentsMargins(0, 0, 0, 0);
  glayout->setSpacing(0);
  glayout->addWidget(this->Internals->TabWidget, 0, 0);

  pqApplicationCore* core = pqApplicationCore::instance();

  core->registerManager("MULTIVIEW_WIDGET", this);

  pqServerManagerModel* smmodel = core->getServerManagerModel();

  QObject::connect(smmodel, SIGNAL(proxyAdded(pqProxy*)), this, SLOT(proxyAdded(pqProxy*)));
  QObject::connect(smmodel, SIGNAL(proxyRemoved(pqProxy*)), this, SLOT(proxyRemoved(pqProxy*)));
  QObject::connect(
    smmodel, SIGNAL(preServerRemoved(pqServer*)), this, SLOT(serverRemoved(pqServer*)));

  this->Internals->addNewTabWidget();
  QObject::connect(
    this->Internals->TabWidget, SIGNAL(currentChanged(int)), this, SLOT(currentTabChanged(int)));

  // we need to ensure that all views loaded from the state do get assigned to
  // some layout correctly.
  QObject::connect(
    core, SIGNAL(stateLoaded(vtkPVXMLElement*, vtkSMProxyLocator*)), this, SLOT(onStateLoaded()));
}

//-----------------------------------------------------------------------------
pqTabbedMultiViewWidget::~pqTabbedMultiViewWidget()
{
  delete this->Internals;
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::setReadOnly(bool val)
{
  if (val != this->readOnly())
  {
    this->Internals->TabWidget->setReadOnly(val);
    if (val)
    {
      this->Internals->removeNewTabWidget();
    }
    else
    {
      this->Internals->addNewTabWidget();
    }
  }
}

//-----------------------------------------------------------------------------
bool pqTabbedMultiViewWidget::readOnly() const
{
  return this->Internals->TabWidget->readOnly();
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::setTabVisibility(bool visible)
{
  this->Internals->TabWidget->setTabBarVisibility(visible);
}

//-----------------------------------------------------------------------------
bool pqTabbedMultiViewWidget::tabVisibility() const
{
  return this->Internals->TabWidget->tabBarVisibility();
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::toggleFullScreen()
{
  auto& internals = (*this->Internals);
  if (this->Internals->FullScreenWindow)
  {
    internals.FullScreenWindow->layout()->removeWidget(this->Internals->TabWidget);
    this->layout()->addWidget(internals.TabWidget);
    delete internals.FullScreenWindow;
    Q_EMIT this->fullScreenEnabled(false);
  }
  else
  {
    QWidget* fullScreenWindow = new QWidget(this, Qt::Window);
    internals.FullScreenWindow = fullScreenWindow;
    fullScreenWindow->setObjectName("FullScreenWindow");
    this->layout()->removeWidget(internals.TabWidget);

    QGridLayout* glayout = new QGridLayout(fullScreenWindow);
    glayout->setSpacing(0);
    glayout->setContentsMargins(0, 0, 0, 0);
    glayout->addWidget(internals.TabWidget, 0, 0);
    fullScreenWindow->showFullScreen();
    fullScreenWindow->show();

    QShortcut* esc = new QShortcut(Qt::Key_Escape, fullScreenWindow);
    QObject::connect(esc, SIGNAL(activated()), this, SLOT(toggleFullScreen()));
    QShortcut* f11 = new QShortcut(QKeySequence(Qt::Key_F11), fullScreenWindow);
    QObject::connect(f11, SIGNAL(activated()), this, SLOT(toggleFullScreen()));
    Q_EMIT this->fullScreenEnabled(true);
  }

  // when we enter full screen, let's hide decorations by default.
  internals.setDecorationsVisibility(internals.FullScreenWindow == nullptr);
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::toggleFullScreenActiveView()
{
  auto& internals = (*this->Internals);
  if (internals.FullScreenWindow)
  {
    // Restore active widget to active frame
    auto widgets = internals.FullScreenWindow->findChildren<pqQVTKWidget*>();
    for (auto wid : widgets)
    {
      internals.ActiveFrame->layout()->addWidget(wid);
    }

    // Restore central tabwidget
    this->layout()->addWidget(internals.TabWidget);
    delete internals.FullScreenWindow;
    Q_EMIT this->fullScreenActiveViewEnabled(false);
  }
  else
  {
    // Hide central tabwidget
    this->layout()->removeWidget(internals.TabWidget);

    QWidget* fullScreenWindow = new QWidget(this, Qt::Window);
    internals.FullScreenWindow = fullScreenWindow;
    fullScreenWindow->setObjectName("FullScreenWindow");

    QGridLayout* glayout = new QGridLayout(fullScreenWindow);
    glayout->setSpacing(0);
    glayout->setContentsMargins(0, 0, 0, 0);

    pqMultiViewWidget* multiViewWidget =
      internals.findWidget(pqActiveObjects::instance().activeLayout());
    pqViewFrame* activeViewFrame = multiViewWidget->activeFrame();
    if (!activeViewFrame)
    {
      qWarning() << "Unable to retrieve the active view frame from the active layout.";
      return;
    }

    // We are in the active view frame, retrieve the widget containing
    // the renderview to show it fullscreen
    QList<pqQVTKWidget*> widgets = activeViewFrame->findChildren<pqQVTKWidget*>();
    for (pqQVTKWidget* wid : widgets)
    {
      // Keep trace of the parent frame to restore it
      // when exiting fullcreen
      internals.ActiveFrame = qobject_cast<QFrame*>(wid->parent());
      if (!internals.ActiveFrame)
      {
        qWarning() << "Unable to retrieve parent frame of render widget.";
        delete internals.FullScreenWindow;
        return;
      }
      // Layout takes widget ownership
      glayout->addWidget(wid, 0, 0);
    }

    fullScreenWindow->showFullScreen();
    fullScreenWindow->show();

    QShortcut* esc = new QShortcut(Qt::Key_Escape, fullScreenWindow);
    QObject::connect(esc, SIGNAL(activated()), this, SLOT(toggleFullScreenActiveView()));
    QShortcut* ctrlF11 =
      new QShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_F11), fullScreenWindow);
    QObject::connect(ctrlF11, SIGNAL(activated()), this, SLOT(toggleFullScreenActiveView()));
    Q_EMIT this->fullScreenActiveViewEnabled(true);
  }
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::proxyAdded(pqProxy* proxy)
{
  if (proxy->getSMGroup() == "layouts" && proxy->getProxy()->IsA("vtkSMViewLayoutProxy"))
  {
    auto vlayout = vtkSMViewLayoutProxy::SafeDownCast(proxy->getProxy());
    this->createTab(vlayout);
  }
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::proxyRemoved(pqProxy* proxy)
{
  auto& internals = (*this->Internals);
  if (proxy->getSMGroup() == "layouts" && proxy->getProxy()->IsA("vtkSMViewLayoutProxy"))
  {
    internals.removeTab(proxy->getProxy());
  }
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::serverRemoved(pqServer* server)
{
  // remove all tabs corresponding to the closed session.
  auto& internals = (*this->Internals);
  internals.removeTabs(server);
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::currentTabChanged(int /* index*/)
{
  QWidget* currentWidget = this->Internals->TabWidget->currentWidget();
  if (pqMultiViewWidget* frame = qobject_cast<pqMultiViewWidget*>(currentWidget))
  {
    frame->makeFrameActive();
  }
  else if (currentWidget == this->Internals->NewTabWidget &&
    this->Internals->TabWidget->count() > 1)
  {
    // count() > 1 check keeps this widget from creating new tabs as the tabs are
    // being removed.
    this->Internals->setCurrentTab(this->createTab());
  }
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::closeTab(int index)
{
  pqMultiViewWidget* widget =
    qobject_cast<pqMultiViewWidget*>(this->Internals->TabWidget->widget(index));
  vtkSMProxy* vlayout = widget ? widget->layoutManager() : nullptr;
  if (vlayout)
  {
    pqServerManagerModel* smmodel = pqApplicationCore::instance()->getServerManagerModel();
    pqObjectBuilder* builder = pqApplicationCore::instance()->getObjectBuilder();

    BEGIN_UNDO_SET(tr("Remove View Tab"));
    // first remove each of the views in the tab layout.
    widget->destroyAllViews();

    SM_SCOPED_TRACE(CallFunction).arg("RemoveLayout").arg(vlayout);

    builder->destroy(smmodel->findItem<pqProxy*>(vlayout));
    END_UNDO_SET();
  }

  // The '+' tab is missing if in readOnly mode. Account for that here.
  int minimumNumberOfTabs = this->readOnly() ? 0 : 1;
  if (this->Internals->TabWidget->count() == minimumNumberOfTabs)
  {
    this->Internals->setCurrentTab(this->createTab());
  }
}

//-----------------------------------------------------------------------------
int pqTabbedMultiViewWidget::createTab()
{
  pqServer* server = pqActiveObjects::instance().activeServer();
  return server ? this->createTab(server) : -1;
}

//-----------------------------------------------------------------------------
int pqTabbedMultiViewWidget::createTab(pqServer* server)
{
  if (server)
  {
    BEGIN_UNDO_SET(tr("Add View Tab"));
    auto pxm = server->proxyManager();
    auto vlayout = pxm->NewProxy("misc", "ViewLayout");
    Q_ASSERT(vlayout != nullptr);

    vtkNew<vtkSMParaViewPipelineControllerWithRendering> controller;
    controller->InitializeProxy(vlayout);
    controller->RegisterLayoutProxy(vlayout);
    vlayout->FastDelete();
    END_UNDO_SET();

    this->updateVisibleTabs();
    return this->Internals->tabIndex(vlayout);
  }
  return -1;
}

//-----------------------------------------------------------------------------
int pqTabbedMultiViewWidget::createTab(vtkSMViewLayoutProxy* vlayout)
{
  auto& internals = (*this->Internals);
  return internals.addTab(vlayout, this);
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::setCurrentTab(int index)
{
  this->Internals->setCurrentTab(index);
}

//-----------------------------------------------------------------------------
bool pqTabbedMultiViewWidget::eventFilter(QObject* obj, QEvent* evt)
{
  // filtering events on the QLabel added as the tabButton to the tabbar to
  // close the tabs. If clicked, we close the tab.
  if (evt->type() == QEvent::MouseButtonRelease && qobject_cast<QLabel*>(obj))
  {
    QMouseEvent* mouseEvent = dynamic_cast<QMouseEvent*>(evt);
    if (mouseEvent->button() == Qt::LeftButton)
    {
      int index =
        this->Internals->TabWidget->tabButtonIndex(qobject_cast<QWidget*>(obj), QTabBar::RightSide);
      if (index != -1)
      {
        BEGIN_UNDO_SET(tr("Close Tab"));
        this->closeTab(index);
        END_UNDO_SET();
        return true;
      }

      // user clicked on the popout label. We pop the frame out (or back in).
      index =
        this->Internals->TabWidget->tabButtonIndex(qobject_cast<QWidget*>(obj), QTabBar::LeftSide);
      if (index != -1)
      {
        // Pop out tab in a separate window.
        pqMultiViewWidget* tabPage =
          qobject_cast<pqMultiViewWidget*>(this->Internals->TabWidget->widget(index));
        if (tabPage)
        {
          QLabel* label = qobject_cast<QLabel*>(obj);
          bool popped_out = tabPage->togglePopout();
          label->setPixmap(label->style()
                             ->standardIcon(pqTabWidget::popoutLabelPixmap(popped_out))
                             .pixmap(PQTABBED_WIDGET_PIXMAP_SIZE, PQTABBED_WIDGET_PIXMAP_SIZE));
          label->setToolTip(
            pqWidgetUtilities::formatTooltip(pqTabWidget::popoutLabelText(popped_out)));
          label->setStatusTip(pqTabWidget::popoutLabelText(popped_out));
        }
        return true;
      }
    }
  }

  return this->Superclass::eventFilter(obj, evt);
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::toggleWidgetDecoration()
{
  auto& internals = (*this->Internals);
  this->setDecorationsVisibility(!internals.decorationsVisibility());
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::setDecorationsVisibility(bool val)
{
  auto& internals = (*this->Internals);
  internals.setDecorationsVisibility(val);
}

//-----------------------------------------------------------------------------
bool pqTabbedMultiViewWidget::decorationsVisibility() const
{
  auto& internals = (*this->Internals);
  return internals.decorationsVisibility();
}

//-----------------------------------------------------------------------------
QSize pqTabbedMultiViewWidget::clientSize() const
{
  if (this->Internals->TabWidget->currentWidget())
  {
    return this->Internals->TabWidget->currentWidget()->size();
  }

  return this->size();
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::lockViewSize(const QSize& viewSize)
{
  QList<QPointer<pqMultiViewWidget>> widgets = this->Internals->widgets();
  Q_FOREACH (QPointer<pqMultiViewWidget> widget, widgets)
  {
    if (widget)
    {
      widget->lockViewSize(viewSize);
    }
  }

  Q_EMIT this->viewSizeLocked(!viewSize.isEmpty());
}

//-----------------------------------------------------------------------------
QSize pqTabbedMultiViewWidget::preview(const QSize& nsize)
{
  return this->Internals->TabWidget->preview(nsize);
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::reset()
{
  for (int cc = this->Internals->TabWidget->count() - 1; cc > 1; --cc)
  {
    this->closeTab(cc - 1);
  }

  pqMultiViewWidget* widget =
    qobject_cast<pqMultiViewWidget*>(this->Internals->TabWidget->currentWidget());
  if (widget)
  {
    widget->reset();
  }
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::onStateLoaded()
{
// FIXME: remove
// maybe warn there are views in state file not assigned to layout
#if 0
  QSet<vtkSMViewProxy*> proxies;
  Q_FOREACH (pqMultiViewWidget* wdg, this->Internals->TabWidgets.values())
  {
    if (wdg)
    {
      proxies.unite(wdg->viewProxies().toSet());
    }
  }

  // check that all views are assigned to some frame or other.
  QList<pqView*> views =
    pqApplicationCore::instance()->getServerManagerModel()->findItems<pqView*>();

  Q_FOREACH (pqView* view, views)
  {
    if (!proxies.contains(view->getViewProxy()))
    {
      this->assignToFrame(view, false);
    }
  }
#endif
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::contextMenuRequested(const QPoint& point)
{
  this->setFocus(Qt::OtherFocusReason);

  int tabIndex = this->Internals->TabWidget->tabBar()->tabAt(point);
  pqMultiViewWidget* widget =
    qobject_cast<pqMultiViewWidget*>(this->Internals->TabWidget->widget(tabIndex));
  auto vlayout = widget ? vtkSMViewLayoutProxy::SafeDownCast(widget->layoutManager()) : nullptr;
  if (!vlayout)
  {
    return;
  }
  pqServerManagerModel* smmodel = pqApplicationCore::instance()->getServerManagerModel();
  pqProxy* proxy = smmodel->findItem<pqProxy*>(vlayout);

  QMenu* menu = new QMenu(this);
  QAction* renameAction = menu->addAction(tr("Rename"));
  QAction* closeAction = menu->addAction(tr("Close layout"));
  auto equalizeMenu = menu->addMenu(tr("Equalize Views"));
  QAction* horizontalAction = equalizeMenu->addAction(tr("Horizontally"));
  equalizeMenu->addAction(tr("Vertically"));
  QAction* bothAction = equalizeMenu->addAction(tr("Both"));

  QAction* action = menu->exec(this->Internals->TabWidget->tabBar()->mapToGlobal(point));
  if (action == closeAction)
  {
    BEGIN_UNDO_SET(tr("Close Tab"));
    this->closeTab(tabIndex);
    END_UNDO_SET();
  }
  else if (action == renameAction)
  {
    bool ok;
    QString oldName = proxy->getSMName();
    QString newName = QInputDialog::getText(
      this, tr("Rename Layout..."), tr("New name:"), QLineEdit::Normal, oldName, &ok);
    if (ok && !newName.isEmpty() && newName != oldName)
    {
      SM_SCOPED_TRACE(CallFunction)
        .arg("RenameLayout")
        .arg(newName.toUtf8().data())
        .arg((vtkObject*)proxy->getProxy());

      proxy->rename(newName);
    }
  }
  else if (action == bothAction)
  {
    vlayout->EqualizeViews();
  }
  else if (action)
  {
    vlayout->EqualizeViews(action == horizontalAction ? vtkSMViewLayoutProxy::HORIZONTAL
                                                      : vtkSMViewLayoutProxy::VERTICAL);
  }

  delete menu;
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::onLayoutNameChanged(pqServerManagerModelItem* item)
{
  pqProxy* proxy = dynamic_cast<pqProxy*>(item);
  for (int i = 0; i < this->Internals->TabWidget->count(); i++)
  {
    pqMultiViewWidget* wdg =
      dynamic_cast<pqMultiViewWidget*>(this->Internals->TabWidget->widget(i));
    if (wdg && wdg->layoutManager() == proxy->getProxy())
    {
      this->Internals->TabWidget->setTabText(i, proxy->getSMName());
      return;
    }
  }
}

//-----------------------------------------------------------------------------
vtkSMViewLayoutProxy* pqTabbedMultiViewWidget::layoutProxy() const
{
  if (auto widget = qobject_cast<pqMultiViewWidget*>(this->Internals->TabWidget->currentWidget()))
  {
    return widget->layoutManager();
  }
  return nullptr;
}

//-----------------------------------------------------------------------------
pqMultiViewWidget* pqTabbedMultiViewWidget::findTab(vtkSMViewLayoutProxy* layoutManager) const
{
  const auto& internals = (*this->Internals);
  return internals.findWidget(layoutManager);
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::enableAnnotationFilter(const QString& annotationKey)
{
  auto& internals = (*this->Internals);
  internals.enableAnnotationFilter(annotationKey);
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::disableAnnotationFilter()
{
  auto& internals = (*this->Internals);
  internals.disableAnnotationFilter();
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::setAnnotationFilterMatching(bool matching)
{
  auto& internals = (*this->Internals);
  internals.setAnnotationFilterMatching(matching);
}

//-----------------------------------------------------------------------------
void pqTabbedMultiViewWidget::updateVisibleTabs()
{
  auto& internals = (*this->Internals);
  internals.updateVisibleTabs();
}

//-----------------------------------------------------------------------------
QList<QString> pqTabbedMultiViewWidget::visibleTabLabels() const
{
  QList<QString> result;
  auto& internals = (*this->Internals);
  auto tabWidget = internals.TabWidget;
  for (int cc = 0, max = tabWidget->count(); cc < max; ++cc)
  {
    result.push_back(tabWidget->tabText(cc));
  }
  return result;
}
