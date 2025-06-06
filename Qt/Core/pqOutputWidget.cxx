// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-FileCopyrightText: Copyright (c) Sandia Corporation
// SPDX-License-Identifier: BSD-3-Clause
#include "pqOutputWidget.h"
#include "ui_pqOutputWidget.h"

#include "pqApplicationCore.h"
#include "pqCoreUtilities.h"
#include "pqFileDialog.h"
#include "pqServer.h"
#include "pqSettings.h"
#include "pqWidgetUtilities.h"
#include "vtkLogger.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkOutputWindow.h"

#include "vtkSMSessionProxyManager.h"

#include <QClipboard>
#include <QDebug>
#include <QDockWidget>
#include <QMutexLocker>
#include <QPointer>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QStandardItemModel>
#include <QStringList>
#include <QStyle>
#include <QStyleHints>

namespace OutputWidgetInternals
{
/// Used when pqOutputWidget is registered with vtkOutputWindow as the default
/// output window for VTK messages.
class OutputWindow : public vtkOutputWindow
{
  QtMsgType ConvertMessageType(const MessageTypes& type)
  {
    switch (type)
    {
      case vtkOutputWindow::MESSAGE_TYPE_TEXT:
        return QtInfoMsg;
      case vtkOutputWindow::MESSAGE_TYPE_ERROR:
        return QtCriticalMsg;
      case vtkOutputWindow::MESSAGE_TYPE_WARNING:
      case vtkOutputWindow::MESSAGE_TYPE_GENERIC_WARNING:
        return QtWarningMsg;
      case vtkOutputWindow::MESSAGE_TYPE_DEBUG:
        return QtDebugMsg;
    }
    return QtInfoMsg;
  }

public:
  static OutputWindow* New();
  vtkTypeMacro(OutputWindow, vtkOutputWindow);
  void SetWidget(pqOutputWidget* widget) { this->Widget = widget; }

  void DisplayText(const char* msg) override
  {
    static thread_local bool callingDisplayText = false;
    if (callingDisplayText)
    {
      // Note: This line will cause trouble if you redirect the VTK logger to this output window.
      vtkLog(WARNING, "Detected reentry into OutputWindow display text with message: " << msg);
      // Quitting so that the mutex below does not lock (and so we don't get in an infinite loop).
      return;
    }
    QMutexLocker locker(&this->MutexGenericMessage);
    QScopedValueRollback callingDisplayTextReset(callingDisplayText, true);
    const auto msgType = this->ConvertMessageType(this->GetCurrentMessageType());
    if (this->Widget)
    {
      const QString qmsg(msg);
      MessageHandler::handlerVTK(msgType, qmsg);
      if (this->Widget->suppress(qmsg, msgType))
      {
        return;
      }
    }
    this->Superclass::DisplayText(msg);
  }

protected:
  OutputWindow() { this->PromptUserOff(); }
  ~OutputWindow() override = default;

  QPointer<pqOutputWidget> Widget;
  QMutex MutexGenericMessage;

private:
  OutputWindow(const OutputWindow&) = delete;
  void operator=(const OutputWindow&) = delete;
};
vtkStandardNewMacro(OutputWindow);
}

MessageHandler::MessageHandler(QObject* parent)
  : QObject(parent)
{
  qRegisterMetaType<QtMsgType>();
  connect(this, &MessageHandler::message, this, &MessageHandler::displayMessage);
}

void MessageHandler::install(pqOutputWidget* widget)
{
  auto self = MessageHandler::instance();
  qInstallMessageHandler(MessageHandler::handler);
  if (widget)
  {
    connect(self, &MessageHandler::showMessage, widget, &pqOutputWidget::displayMessage);
  }
}

void MessageHandler::handler(QtMsgType type, const QMessageLogContext& cntxt, const QString& msg)
{
  QString formattedMsg = qFormatLogMessage(type, cntxt, msg);
  formattedMsg += "\n";
  Q_EMIT instance()->message(type, formattedMsg);
}

void MessageHandler::handlerVTK(QtMsgType type, const QString& msg)
{
  Q_EMIT instance()->showMessage(msg, type);
}

MessageHandler* MessageHandler::instance()
{
  static MessageHandler instance;
  return &instance;
}

void MessageHandler::displayMessage(QtMsgType type, const QString& msg)
{
  QByteArray localMsg = msg.toUtf8();
  vtkOutputWindow* vtkWindow = vtkOutputWindow::GetInstance();
  if (vtkWindow)
  {
    switch (type)
    {
      case QtDebugMsg:
        vtkWindow->DisplayDebugText(localMsg.data());
        break;

      case QtInfoMsg:
        vtkWindow->DisplayText(localMsg.data());
        break;

      case QtWarningMsg:
        vtkWindow->DisplayWarningText(localMsg.data());
        break;

      case QtCriticalMsg:
      case QtFatalMsg:
        vtkWindow->DisplayErrorText(localMsg.data());
        break;
    }
  }
}

class pqOutputWidget::pqInternals
{
  pqOutputWidget* Parent;
  static const int COLUMN_COUNT = 1;
  static const int COLUMN_DATA = 0;

  static QStandardItem* newEmptyItem()
  {
    auto item = new QStandardItem();
    item->setFlags(item->flags() ^ Qt::ItemIsEditable);
    return item;
  }

public:
  bool FirstShowFinished = false;
  Ui::OutputWidget Ui;
  QPointer<QStandardItemModel> Model;
  vtkNew<OutputWidgetInternals::OutputWindow> VTKOutputWindow;
  QStringList SuppressedStrings;

  pqInternals(pqOutputWidget* self)
    : Parent(self)
  {
    this->Ui.setupUi(self);
    pqWidgetUtilities::formatChildTooltips(self);
    this->Ui.filterButton->hide(); // for now. not sure how useful the filter is.
    this->Model = new QStandardItemModel(self);
    this->Model->setColumnCount(2);
    this->Ui.treeView->setModel(this->Model);
    this->Ui.treeView->header()->moveSection(COLUMN_COUNT, COLUMN_DATA);

    this->VTKOutputWindow->SetWidget(self);

    // this list needs to be reevaluated. For now, leaving it untouched.
    this->SuppressedStrings << "QEventDispatcherUNIX::unregisterTimer"
                            << "looking for 'HistogramView"
                            << "(looking for 'XYPlot"
                            << "Unrecognised OpenGL version"
                            /* Skip DBusMenuExporterPrivate errors. These, I suspect, are due to
                             * repeated menu actions in the menus. */
                            << "DBusMenuExporterPrivate"
                            << "DBusMenuExporterDBus"
                            // This error appears in Qt 5.6 on Mac OS X 10.11.1 (and maybe others)
                            << "QNSView mouseDragged: Internal mouse button tracking invalid"
                            << "Unrecognised OpenGL version"
                            /* Skip DBusMenuExporterPrivate errors. These, I suspect, are due to
                             * repeated menu actions in the menus. */
                            << "DBusMenuExporterPrivate"
                            << "DBusMenuExporterDBus"
                            // Skip XCB errors coming from Qt 5 tests.
                            << "QXcbConnection: XCB"
                            /* This error message appears on some HDPi screens with not clear
                               reasons */
                            << "QWindowsWindow::setGeometry: Unable to set geometry"
                            // Skip qt.qpa.xcb errors
                            << "qt.qpa.xcb: internal error"
                            /* suppress "warning: internal error:  void
                               QXcbWindow::setNetWmStateOnUnmappedWindow() called on mapped window"
                               */
                            << "QXcbWindow::setNetWmStateOnUnmappedWindow"
                            /* suppress "warning: In unknown, line 0" and
                               "warning: Populating font family aliases took"
                             */
                            << "warning: In unknown, line 0"
                            << "warning: Populating font family aliases took";
  }

  void displayMessageInConsole(const QString& message, QtMsgType type)
  {
    QTextCharFormat originalFormat = this->Ui.consoleWidget->getFormat();
    QTextCharFormat curFormat(originalFormat);
    curFormat.setForeground(this->foregroundColor(type));
    curFormat.clearBackground();
    this->Ui.consoleWidget->setFormat(curFormat);
    this->Ui.consoleWidget->printString(message);
    this->Ui.consoleWidget->setFormat(originalFormat);
  }

  void addMessageToTree(const QString& message, QtMsgType type, const QString& summary)
  {
    // Check if message is duplicate of the last one. If so, we just increment
    // the counter.
    QStandardItem* rootItem = this->Model->invisibleRootItem();
    int lastIndex = rootItem->rowCount() - 1;
    if (lastIndex >= 0)
    {
      QStandardItem* lastSummaryItem = rootItem->child(lastIndex, COLUMN_DATA);
      QStandardItem* lastMessageItem = lastSummaryItem->child(0, COLUMN_DATA);
      if (lastSummaryItem->text() == summary && lastMessageItem->text() == message)
      {
        QStandardItem* lastSummaryCount = rootItem->child(lastIndex, COLUMN_COUNT);
        int count = lastSummaryCount->text().toInt();
        count = (count <= 0) ? 2 : count + 1;
        lastSummaryCount->setText(QString::number(count));
        lastSummaryCount->setTextAlignment(Qt::AlignRight);
        return;
      }
    }

    QStandardItem* summaryItem = new QStandardItem(summary);
    summaryItem->setFlags(summaryItem->flags() ^ Qt::ItemIsEditable);
    summaryItem->setForeground(this->foregroundColor(type));
    summaryItem->setIcon(this->icon(type));
    summaryItem->setData(QVariant(Qt::AlignLeft | Qt::AlignTop), Qt::TextAlignmentRole);

    QStandardItem* messageItem = new QStandardItem(message);
    messageItem->setFlags(messageItem->flags() ^ Qt::ItemIsEditable);
    messageItem->setForeground(this->foregroundColor(type));
    messageItem->setData(QVariant(Qt::AlignLeft | Qt::AlignTop), Qt::TextAlignmentRole);

    QList<QStandardItem*> items;
    items << messageItem << this->newEmptyItem();
    summaryItem->appendRow(items);
    items.clear();

    items << summaryItem << this->newEmptyItem();
    rootItem->appendRow(items);
  }

  void clear()
  {
    this->Model->clear();
    this->Ui.consoleWidget->clear();
    this->Model->setColumnCount(2);
    this->Ui.treeView->header()->moveSection(COLUMN_COUNT, COLUMN_DATA);
  }

  void setFontSize(int fontSize)
  {
    this->Ui.consoleWidget->setFontSize(fontSize);

    QFont font;
    font.setPointSize(fontSize);
    this->Ui.treeView->setFont(font);
  }

  QIcon icon(QtMsgType type)
  {
    switch (type)
    {
      case QtDebugMsg:
        return this->Parent->style()->standardIcon(QStyle::SP_MessageBoxInformation);

      case QtCriticalMsg:
      case QtFatalMsg:
        return this->Parent->style()->standardIcon(QStyle::SP_MessageBoxCritical);

      case QtWarningMsg:
        return this->Parent->style()->standardIcon(QStyle::SP_MessageBoxWarning);

      case QtInfoMsg:
      default:
        return QIcon();
    }
  }

  QColor foregroundColor(QtMsgType type)
  {
    switch (type)
    {
      case QtInfoMsg:
      case QtDebugMsg:
        return (pqCoreUtilities::isDarkTheme() ? QColor(Qt::green) : QColor(Qt::darkGreen));

      case QtCriticalMsg:
      case QtFatalMsg:
      case QtWarningMsg:
        return (pqCoreUtilities::isDarkTheme() ? QColor(Qt::red) : QColor(Qt::darkRed));

      default:
        return QApplication::palette().color(QPalette::Text);
    }
  }

  void setSettingsKey(const QString& key)
  {
    this->SettingsKey = key;
    if (!key.isEmpty())
    {
      pqSettings* settings = pqApplicationCore::instance()->settings();
      this->Parent->showFullMessages(
        settings->value(QString("%1.ShowFullMessages").arg(key), false).toBool());
      this->Parent->alwaysOpenForNewMessages(
        settings->value(QString("%1.AlwaysOpenForNewMessages").arg(key), true).toBool());
    }
  }

  void saveSetting(const QString& settingName, const QVariant& value)
  {
    if (!this->SettingsKey.isEmpty() && !settingName.isEmpty())
    {
      pqSettings* settings = pqApplicationCore::instance()->settings();
      settings->setValue(QString("%1.%2").arg(this->SettingsKey).arg(settingName), value);
    }
  }

  const QString& settingsKey() const { return this->SettingsKey; }

  /**
   * add a list of strings to be subpressed
   * this is thread safe.
   */
  void suppress(const QStringList& substrs)
  {
    QMutexLocker locker(&this->SuppressionMutex);
    this->SuppressedStrings.append(substrs);
  }

  /**
   * returns true if the message should be/is suppressed.
   * this is thread safe.
   */
  bool suppress(const QString& message, QtMsgType)
  {
    QMutexLocker locker(&this->SuppressionMutex);
    Q_FOREACH (const QString& substr, this->SuppressedStrings)
    {
      if (message.contains(substr))
      {
        return true;
      }
    }
    return false;
  }

private:
  QString SettingsKey;
  QMutex SuppressionMutex;
};

//-----------------------------------------------------------------------------
pqOutputWidget::pqOutputWidget(QWidget* parentObject, Qt::WindowFlags f)
  : Superclass(parentObject, f)
  , Internals(new pqOutputWidget::pqInternals(this))
{
  // Setup Qt message pattern
  qSetMessagePattern("%{type}: In %{file}, line %{line}\n%{type}: %{message}");

  pqInternals& internals = (*this->Internals);

  this->connect(
    internals.Ui.showFullMessagesCheckBox, SIGNAL(toggled(bool)), SLOT(showFullMessages(bool)));
  this->connect(internals.Ui.alwaysOpenForNewMessagesCheckBox, SIGNAL(toggled(bool)),
    SLOT(alwaysOpenForNewMessages(bool)));
  this->connect(internals.Ui.saveButton, SIGNAL(clicked()), SLOT(saveToFile()));
  this->connect(internals.Ui.copyButton, SIGNAL(clicked()), SLOT(copyToClipboard()));

  // Tell VTK to forward all messages.
  vtkOutputWindow::SetInstance(internals.VTKOutputWindow.Get());

  // Install the message handler
  MessageHandler::install(this);
}

//-----------------------------------------------------------------------------
pqOutputWidget::~pqOutputWidget()
{
  if (vtkOutputWindow::GetInstance() == this->Internals->VTKOutputWindow.Get())
  {
    vtkOutputWindow::SetInstance(nullptr);
  }
}

//-----------------------------------------------------------------------------
void pqOutputWidget::suppress(const QStringList& substrs)
{
  this->Internals->suppress(substrs);
}

//-----------------------------------------------------------------------------
void pqOutputWidget::saveToFile()
{
  QString text = this->Internals->Ui.consoleWidget->text();
  pqServer* server = pqApplicationCore::instance()->getActiveServer();
  pqFileDialog fileDialog(server, pqCoreUtilities::mainWidget(), tr("Save output"), QString(),
    tr("Text Files") + " (*.txt);;" + tr("All Files") + " (*)", false, false);
  fileDialog.setFileMode(pqFileDialog::AnyFile);
  if (fileDialog.exec() != pqFileDialog::Accepted)
  {
    // Canceled
    return;
  }

  QString filename = fileDialog.getSelectedFiles()[0];
  QByteArray filename_ba = filename.toUtf8();
  vtkTypeUInt32 location = fileDialog.getSelectedLocation();
  auto pxm = server->proxyManager();
  if (!pxm->SaveString(text.toStdString().c_str(), filename_ba.data(), location))
  {
    qCritical() << tr("Failed to save output to ") << filename;
  }
}

//-----------------------------------------------------------------------------
void pqOutputWidget::copyToClipboard()
{
  QClipboard* clipboard = QGuiApplication::clipboard();
  clipboard->setText(this->Internals->Ui.consoleWidget->text());
}

//-----------------------------------------------------------------------------
void pqOutputWidget::clear()
{
  pqInternals& internals = (*this->Internals);
  internals.clear();
  Q_EMIT this->cleared();
}

//-----------------------------------------------------------------------------
bool pqOutputWidget::displayMessage(const QString& message, QtMsgType type)
{
  QString tmessage = message.trimmed();
  if (!this->suppress(tmessage, type))
  {
    this->Internals->displayMessageInConsole(message, type);
    QString summary = this->extractSummary(tmessage, type);
    this->Internals->addMessageToTree(message, type, summary);

    Q_EMIT this->messageDisplayed(message, type);
    return true;
  }
  return false;
}

//-----------------------------------------------------------------------------
bool pqOutputWidget::suppress(const QString& message, QtMsgType mtype)
{
  return this->Internals->suppress(message, mtype);
}

//-----------------------------------------------------------------------------
QString pqOutputWidget::extractSummary(const QString& message, QtMsgType)
{
  // check if python traceback, if so, simply return the last line as the
  // summary.
  if (message.startsWith("traceback", Qt::CaseInsensitive))
  {
    return message.section('\n', -1);
  }

  QRegularExpression vtkMessage(
    "^(?:error|warning|debug|generic warning): In (.*), line (\\d+)\n[^:]*:(.*)$",
    QRegularExpression::CaseInsensitiveOption);
  auto match = vtkMessage.match(message);
  if (match.hasMatch())
  {
    QString summary = match.captured(3);
    summary.replace('\n', ' ');
    return summary;
  }

  // if couldn't extract summary in a known form, just return the first line.
  return message.left(message.indexOf('\n'));
}

//-----------------------------------------------------------------------------
void pqOutputWidget::alwaysOpenForNewMessages(bool val)
{
  pqInternals& internals = (*this->Internals);
  internals.Ui.alwaysOpenForNewMessagesCheckBox->setChecked(val);
  internals.saveSetting("AlwaysOpenForNewMessages", val);
}

//-----------------------------------------------------------------------------
bool pqOutputWidget::shouldOpenForNewMessages()
{
  pqInternals& internals = (*this->Internals);
  return internals.FirstShowFinished ? internals.Ui.alwaysOpenForNewMessagesCheckBox->isChecked()
                                     : true;
}

//-----------------------------------------------------------------------------
void pqOutputWidget::showEvent(QShowEvent* event)
{
  this->Superclass::showEvent(event);
  pqInternals& internals = (*this->Internals);
  internals.FirstShowFinished = true;

  // if we're docked, then disable 'Always open for new messages' checkbox
  QDockWidget* dock = qobject_cast<QDockWidget*>(this->parentWidget());
  if (dock != nullptr && !dock->isFloating())
  {
    internals.Ui.alwaysOpenForNewMessagesCheckBox->setEnabled(false);
  }
  else
  {
    internals.Ui.alwaysOpenForNewMessagesCheckBox->setEnabled(true);
  }
}

//-----------------------------------------------------------------------------
void pqOutputWidget::showFullMessages(bool val)
{
  pqInternals& internals = (*this->Internals);
  internals.Ui.showFullMessagesCheckBox->setChecked(val);
  internals.Ui.stackedWidget->setCurrentIndex(val ? 1 : 0);
  internals.saveSetting("ShowFullMessages", val);
}

//-----------------------------------------------------------------------------
void pqOutputWidget::setSettingsKey(const QString& key)
{
  pqInternals& internals = (*this->Internals);
  internals.setSettingsKey(key);
}

//-----------------------------------------------------------------------------
const QString& pqOutputWidget::settingsKey() const
{
  const pqInternals& internals = (*this->Internals);
  return internals.settingsKey();
}

//-----------------------------------------------------------------------------
void pqOutputWidget::setFontSize(int fontSize)
{
  this->Internals->setFontSize(fontSize);
}
