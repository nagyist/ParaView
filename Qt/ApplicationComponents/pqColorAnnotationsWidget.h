/*=========================================================================

   Program: ParaView
   Module:  pqColorAnnotationsWidget.cxx

   Copyright (c) 2005,2006 Sandia Corporation, Kitware Inc.
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

========================================================================*/
#ifndef pqColorAnnotationsWidget_h
#define pqColorAnnotationsWidget_h

#include "pqApplicationComponentsModule.h"

#include "pqAnnotationsModel.h" // for pqAnnotationsModel::ColumnRoles

#include <QWidget>

class QItemSelection;
class QModelIndex;

class vtkSMProxy;

/**
 * pqColorAnnotationsWidget is used to edit annotations and colors
 */
class PQAPPLICATIONCOMPONENTS_EXPORT pqColorAnnotationsWidget : public QWidget
{
  Q_OBJECT;
  typedef QWidget Superclass;

public:
  pqColorAnnotationsWidget(QWidget* parent = nullptr);
  ~pqColorAnnotationsWidget() override;

  void setLookupTableProxy(vtkSMProxy* proxy);

  ///@{
  /**
   * Get/Set the annotations.
   * This is a list generated by flattening 2-tuples where 1st value is the
   * annotated value and second is the annotation text.
   */
  QList<QVariant> annotations() const;
  void setAnnotations(const QList<QVariant>&);
  ///@}

  ///@{
  /**
   * Get/Set the indexed colors. This is a list generated by flattening
   * 3-tuples (r,g,b).
   */
  QList<QVariant> indexedColors() const;
  void setIndexedColors(const QList<QVariant>&);
  ///@}

  ///@{
  /**
   * Get/Set the indexed opacities.
   */
  QList<QVariant> indexedOpacities() const;
  void setIndexedOpacities(const QList<QVariant>&);
  ///@}

  ///@{
  /**
   * Get/Set the indexed visibilities.
   */
  QList<QVariant> visibilities() const;
  void setVisibilities(const QList<QVariant>&);
  ///@}

  ///@{
  /**
   * Get/Set the opacity mapping status.
   */
  QVariant opacityMapping() const;
  void setOpacityMapping(const QVariant&);
  ///@}

  /**
   * Apply a color preset using its name
   */
  void applyPreset(const char* presetName);

  /**
   * Ensures that the table for indexedColors are shown only when this
   * is set to true.
   */
  void indexedLookupStateUpdated(bool indexed);

  /**
   * Use active representation to create annotations.
   */
  bool addActiveAnnotations(bool force);

  /**
   * Return current model index from table.
   * Uses QSortFilterProxyModel if exists.
   */
  QModelIndex currentIndex();

  /**
   * Return the selected indexes.
   */
  QModelIndexList selectedIndexes();

  /**
   * Set the annotionModel on the underlying table.
   */
  void setAnnotationsModel(pqAnnotationsModel* model);

  /**
   * Set reordering support on the table.
   */
  void setSupportsReorder(bool reorder);

  /**
   * Sort the underlying proxy model
   */
  void sort(int column, Qt::SortOrder order = Qt::AscendingOrder);

  /**
   * Show/Hide relevant widgets.
   * If allow is false, hide the buttons that add/remove lines.
   */
  void allowsUserDefinedValues(bool allow);

  /**
   * Setup ChooseColor dialog.
   * If allow is true, add a line edit to specify a regexp to do the matching between data values
   * and preset.
   * Intended to be used for series preset.
   */
  void allowsRegexpMatching(bool allow);

  /**
   * Show/Hide relevant widgets.
   * If enable is false, hide the buttons that save / load presets.
   */
  void enablePresets(bool enable);

  /**
   * Show/Hide the Visibility column.
   */
  void supportsVisibilityCheck(bool val);

  /**
   * Show/Hide the EnableOpacityMapping button.
   */
  void supportsOpacityMapping(bool val);

  /**
   * Show/Hide given column.
   */
  void setColumnVisibility(pqAnnotationsModel::ColumnRoles col, bool visible);

  ///@{
  /**
   * Get / Set the name of the current preset in use.
   */
  const char* currentPresetName();
  void setCurrentPresetName(const char* name);
  ///@}

  /**
   * Get the current annotation value.
   */
  QString currentAnnotationValue();

  /**
   * Get the selected annotations values.
   */
  QStringList selectedAnnotations();
  void setSelectedAnnotations(const QStringList& annotations);

  /**
   * Get if annotations should be loaded with the preset.
   */
  bool presetLoadAnnotations();

  /**
   * Get the regular expression to use to apply the preset.
   */
  QRegularExpression presetRegularExpression();

Q_SIGNALS:
  /**
   * Fired when the annotations are changed.
   */
  void annotationsChanged();

  /**
   * Fired when the indexed colors are changed.
   */
  void indexedColorsChanged();

  /**
   * Fired when the indexed opacities are changed.
   */
  void indexedOpacitiesChanged();

  /**
   * Fired when the visibilities are changed.
   */
  void visibilitiesChanged();

  /**
   * Fired when the opacity mapping status is changed.
   */
  void opacityMappingChanged();

  /**
   * Fired when a new preset is applied.
   */
  void presetChanged(const QString& name);

  /**
   * Fired when the currently focused item changed.
   */
  void selectionChanged(const QItemSelection&, const QItemSelection&);

protected:
  /**
   * Create annotations from visibles part of the active representation.
   */
  bool addActiveAnnotationsFromVisibleSources(bool force);

private Q_SLOTS:
  ///@{
  /**
   * slots called when user presses corresponding buttons to add/remove
   * annotations.
   */
  void addAnnotation();
  void removeAnnotation();
  void addActiveAnnotations();
  void addActiveAnnotationsFromVisibleSources();
  void removeAllAnnotations();
  ///@}

  void onPresetApplied(const QString& presetName);

  /**
   * called whenever the internal model's data changes. We fire
   * annotationsChanged(), indexedColorsChanged(), visibilitiesChanged()
   * or indexedOpacitiesChanged() signals appropriately.
   */
  void onDataChanged(const QModelIndex& topleft, const QModelIndex& btmright);

  /**
   * called when user double-clicks on an item. If the double click is on the
   * color column, we show the color editor to allow editing of the indexed
   * color.
   */
  void onDoubleClicked(const QModelIndex& idx);

  /**
   * called when user double-clicks on an item. If the double click is on the
   * color column, we show the color editor to allow editing of the indexed
   * color.
   */
  void onHeaderDoubleClicked(int section);

  /**
   * Called when global and selected opacity should be set
   */
  void execGlobalOpacityDialog();

  /**
   * pick a preset.
   */
  void choosePreset(const char* presetName = nullptr);

  /**
   * save current transfer function as preset.
   */
  void saveAsNewPreset();
  void saveAsPreset(const char* name, bool removeAnnotations, bool allowOverride);

  /**
   * Ensures that the table for indexedColors are shown only when this
   * is set to true.
   */
  void updateOpacityColumnState();

  /**
   * called when the user edits past the last row.
   */
  void editPastLastRow();

private: // NOLINT(readability-redundant-access-specifiers)
  Q_DISABLE_COPY(pqColorAnnotationsWidget)

  class pqInternals;
  pqInternals* Internals;
};

#endif
