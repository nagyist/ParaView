// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-FileCopyrightText: Copyright (c) Sandia Corporation
// SPDX-License-Identifier: BSD-3-Clause
#ifndef pqProgressWidget_h
#define pqProgressWidget_h

#include <QScopedPointer>
#include <QWidget>

#include "pqWidgetsModule.h"

class QToolButton;
class pqProgressWidgetLabel;

/**
 * @class pqProgressWidget
 * @brief widget to show progress in a QStatusBar.
 *
 * pqProgressWidget is a widget designed to be used in the QStatusBar of the
 * application to show progress for time consuming tasks in the application.
 *
 * pqProgressWidget is a replacement for QProgressBar. It has the following
 * differences with QProgressBar.
 *
 * \li 1. It adds support for an abort button that can be enabled to allow
 *        aborting for interruptible processing while progress in active.
 * \li 2. It does not use QMacStyle on OsX instead uses "fusion" or "cleanlooks"
 *        show that the text is shown on the progress bar.
 * \li 3. It does not render progress bar grove for a more "flat" look and avoid
 *        dramatic UI change when toggling between showing progress and not.
 */
class PQWIDGETS_EXPORT pqProgressWidget : public QWidget
{
  Q_OBJECT;
  typedef QWidget Superclass;
  Q_PROPERTY(QString readyText READ readyText WRITE setReadyText)
  Q_PROPERTY(QString busyText READ busyText WRITE setBusyText)
public:
  pqProgressWidget(QWidget* parent = nullptr);
  ~pqProgressWidget() override;

  /**
   * @deprecated in ParaView 5.5. Use `abortButton` instead.
   */
  QToolButton* getAbortButton() const { return this->AbortButton; }

  /**
   * Provides access to the abort button.
   */
  QToolButton* abortButton() const { return this->AbortButton; }

  ///@{
  /**
   * Set the text to use by default when the progress bar is not enabled
   * which typically corresponds to application not being busy.
   * Default value is empty.
   */
  void setReadyText(const QString&);
  const QString& readyText() const { return this->ReadyText; }
  ///@}

  ///@{
  /**
   * Set the text to use by default when the progress bar is enabled
   * which typically corresponds to application being busy.
   * Default value is "Busy".
   */
  void setBusyText(const QString&);
  const QString& busyText() const { return this->BusyText; }
  ///@}

public Q_SLOTS: // NOLINT(readability-redundant-access-specifiers)
  /**
   * Set the progress. Progress must be enabled by calling 'enableProgress`
   * otherwise this method will have no effect.
   */
  void setProgress(const QString& message, int value);

  /**
   * Enabled/disable the progress. This is different from
   * enabling/disabling the widget itself. This shows/hides
   * the progress part of the widget.
   */
  void enableProgress(bool enabled);

  /**
   * Enable/disable the abort button.
   */
  void enableAbort(bool enabled);

Q_SIGNALS:
  /**
   * triggered with the abort button is pressed.
   */
  void abortPressed();

protected:
  pqProgressWidgetLabel* ProgressBar;
  QToolButton* AbortButton;

  /**
   * request Qt to paint the widgets.
   */
  void updateUI();

  /**
   * Overriden to change the application cursor to a "ArrowCursor" when the progress bar is showing
   * progress and the mouse enters the progress widget. This makes it intuitive for the user to know
   * that they can clik on the abort button to stop the progress.
   */
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  void enterEvent(QEvent* e) override;
#else
  void enterEvent(QEnterEvent* e) override;
#endif

  /**
   * Overriden to restore the cursor to the default cursor when the mouse leaves
   * the progress widget.
   */
  void leaveEvent(QEvent* e) override;

private:
  Q_DISABLE_COPY(pqProgressWidget);
  QString ReadyText;
  QString BusyText;
};

#endif
