// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-FileCopyrightText: Copyright (c) Sandia Corporation
// SPDX-License-Identifier: BSD-3-Clause
#ifndef pqChooseColorPresetReaction_h
#define pqChooseColorPresetReaction_h

#include "pqReaction.h"

#include "vtkNew.h"         // needed for vtkNew.
#include "vtkWeakPointer.h" // needed for vtkWeakPointer.

#include <QPointer>           // needed for QPointer
#include <QRegularExpression> // needed for QRegularExpression.

#include <vector> // needed for std::vector.

class pqDataRepresentation;
class pqPresetDialog;
class vtkSMColorMapEditorHelper;
class vtkSMProxy;

/**
 * @ingroup Reactions
 * @class pqChooseColorPresetReaction
 * @brief Reaction to pop up a color palette chooser dialog.
 *
 * pqChooseColorPresetReaction is used by pqColorEditorPropertyWidget (for example)
 * allow the user to change the color palette. It pops up a modal dialog with
 * color presets to select and apply. There are three ways of using this reaction:
 *
 * \li Monitor active representation: To use this mode, pass the \c track_active_objects
 * parameter to the constructor as `true` (default). When in this configuration, the reaction
 * monitors the active representation (via pqActiveObjects) and allows the user to load a
 * preset on the active representation's color/opacity transfer function using the parent
 * action.
 *
 * \li Monitor a specific representation: To use this mode, pass the \c track_active_objects
 * parameter to the constructor as `false`. Then the calling code can explicitly set the
 * representation using pqChooseColorPresetReaction::setRepresentation() method. The reaction will
 * monitor this representation and allow user to load preset of its transfer functions.
 *
 * \li Control a specific color transfer function: To use this mode, pass the \c
 * track_active_objects parameter to the constructor as `false` and then set the transfer function
 * proxy explicitly using pqChooseColorPresetReaction::setTransferFunction() method.
 *
 * In all modes, the reaction will fire the presetApplied() signal to indicate a new preset has been
 * applied. Typically one can hook that signal up to render affected views.
 */
class PQAPPLICATIONCOMPONENTS_EXPORT pqChooseColorPresetReaction : public pqReaction
{
  Q_OBJECT
  typedef pqReaction Superclass;

public:
  pqChooseColorPresetReaction(QAction* parent, bool track_active_objects = true);
  ~pqChooseColorPresetReaction() override;

public Q_SLOTS: // NOLINT(readability-redundant-access-specifiers)
  /**
   * Choose color preset for the representation set using setRepresentation().
   * Returns false if representation cannot be located or its is not using
   * scalar coloring.
   */
  bool choosePreset(const char* presetName = nullptr);

  ///@{
  /**
   * Set the data representation explicitly when track_active_objects is false.
   */
  void setRepresentation(pqDataRepresentation* repr, int selectedPropertiesType);
  void setRepresentation(pqDataRepresentation* repr)
  {
    this->setRepresentation(repr, 0 /*Representation*/);
  }
  void setActiveRepresentation();
  ///@}

  ///@{
  /**
   * Set the transfer function proxy. This can be used, instead of
   * setRepresentation() when \c track_active_objects was false to directly set
   * the transfer function proxy on which to apply the preset.
   */
  void setTransferFunctions(std::vector<vtkSMProxy*> lut);
  void setTransferFunction(vtkSMProxy* lut)
  {
    this->setTransferFunctions(std::vector<vtkSMProxy*>{ lut });
  }
  ///@}

  /**
   * Updates the enabled state. Applications need not explicitly call this.
   */
  void updateEnableState() override;

  /**
   * Show/hide widget in the dialog.
   * Allows a regexp as user entry to do the matching between data values and preset.
   * Intended to be used for series preset.
   */
  void setAllowsRegexpMatching(bool allow) { this->AllowsRegexpMatching = allow; }

  /**
   * Return the regular expression specified in the Dialog.
   */
  QRegularExpression regularExpression();

  /**
   * Return if annotations should be loaded.
   */
  bool loadAnnotations();

Q_SIGNALS:
  /**
   * fired every time a preset is applied.
   */
  void presetApplied(const QString&);

private Q_SLOTS:
  void applyCurrentPreset();

  /**
   * Called to update the transfer function using the pqDataRepresentation
   * set by setRepresentation() call;
   */
  void updateTransferFunction();

protected:
  /**
   * Called when the action is triggered.
   */
  void onTriggered() override;

private:
  Q_DISABLE_COPY(pqChooseColorPresetReaction)
  QPointer<pqDataRepresentation> Representation;
  std::vector<vtkWeakPointer<vtkSMProxy>> TransferFunctionProxies;
  static QPointer<pqPresetDialog> PresetDialog;
  bool AllowsRegexpMatching;
  vtkNew<vtkSMColorMapEditorHelper> ColorMapEditorHelper;
};

#endif
