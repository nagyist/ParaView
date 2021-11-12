/*=========================================================================

   Program: ParaView
   Module:    pqLinksManager.h

   Copyright (c) 2005-2008 Sandia Corporation, Kitware Inc.
   All rights reserved.

   ParaView is a free software; you can redistribute it and/or modify it
   under the terms of the ParaView license version 1.2.

   See License_v1.2.txt for the full ParaView license.
   A copy of this license can be obtained by contacting
   Kitware Inc.
   28 Corporate Drive
   Clifton Park, NY 12065
   USA

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

=========================================================================*/

#ifndef pqLinksManager_h
#define pqLinksManager_h

#include <QDialog>
#include <QModelIndex>
#include <QScopedPointer>

#include "pqComponentsModule.h"

namespace Ui
{
class pqLinksManager;
}
/**
 * dialog for viewing, creating, editing, removing proxy/property/camera links
 */
class PQCOMPONENTS_EXPORT pqLinksManager : public QDialog
{
  Q_OBJECT
  typedef QDialog base;

public:
  /**
   * create this dialog with a parent
   */
  pqLinksManager(QWidget* p = 0);
  /**
   * destroy this dialog
   */
  ~pqLinksManager() override;

public Q_SLOTS: // NOLINT(readability-redundant-access-specifiers)
  /**
   * add a link
   */
  void addLink();
  /**
   * edit the currently selected link
   */
  void editLink();
  /**
   * edit the currently selected link
   */
  void removeLink();

private Q_SLOTS:
  void selectionChanged(const QModelIndex& idx);

private: // NOLINT(readability-redundant-access-specifiers)
  QScopedPointer<Ui::pqLinksManager> Ui;
};

#endif
