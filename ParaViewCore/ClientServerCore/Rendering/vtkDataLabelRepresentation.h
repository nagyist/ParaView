/*=========================================================================

  Program:   ParaView
  Module:    vtkDataLabelRepresentation.h

  Copyright (c) Kitware, Inc.
  All rights reserved.
  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/**
 * @class   vtkDataLabelRepresentation
 * @brief   representation for showing cell and point
 * labels.
 *
 * vtkDataLabelRepresentation is a representation for showing cell and/or point
 * labels. This representation relies on all the data being cloned on all nodes
 * hence beware of using this representation for large datasets.
 * @par Caveat:
 * Note that vtkDataLabelRepresentation adds the label props to the
 * non-composited renderer.
 * @par Thanks:
 * The addition of a transformation matrix was supported by CEA/DIF
 * Commissariat a l'Energie Atomique, Centre DAM Ile-De-France, Arpajon, France.
*/

#ifndef vtkDataLabelRepresentation_h
#define vtkDataLabelRepresentation_h

#include "vtkPVClientServerCoreRenderingModule.h" //needed for exports
#include "vtkPVDataRepresentation.h"
#include "vtkSmartPointer.h" // needed for vtkSmartPointer.

class vtkActor2D;
class vtkCellCenters;
class vtkCallbackCommand;
class vtkCompositeDataToUnstructuredGridFilter;
class vtkLabeledDataMapper;
class vtkProp3D;
class vtkPVCacheKeeper;
class vtkTextProperty;
class vtkTransform;

class VTKPVCLIENTSERVERCORERENDERING_EXPORT vtkDataLabelRepresentation
  : public vtkPVDataRepresentation
{
public:
  static vtkDataLabelRepresentation* New();
  vtkTypeMacro(vtkDataLabelRepresentation, vtkPVDataRepresentation);
  void PrintSelf(ostream& os, vtkIndent indent) VTK_OVERRIDE;

  /**
   * This needs to be called on all instances of vtkGeometryRepresentation when
   * the input is modified. This is essential since the geometry filter does not
   * have any real-input on the client side which messes with the Update
   * requests.
   */
  virtual void MarkModified() VTK_OVERRIDE;

  //@{
  /**
   * Get/Set the visibility for this representation. When the visibility of
   * representation of false, all view passes are ignored.
   */
  virtual void SetVisibility(bool val) VTK_OVERRIDE;
  virtual bool GetVisibility() VTK_OVERRIDE;
  //@}

  //***************************************************************************
  // Methods to change various parameters on internal objects
  void SetPointLabelVisibility(int);
  void SetPointFieldDataArrayName(const char*);
  void SetPointLabelMode(int);
  void SetPointLabelColor(double r, double g, double b);
  void SetPointLabelOpacity(double);
  void SetPointLabelFontFamily(int);
  void SetPointLabelBold(int);
  void SetPointLabelItalic(int);
  void SetPointLabelShadow(int);
  void SetPointLabelJustification(int);
  void SetPointLabelFontSize(int);
  void SetPointLabelFormat(const char*);

  void SetCellLabelVisibility(int);
  void SetCellFieldDataArrayName(const char*);
  void SetCellLabelMode(int);
  void SetCellLabelColor(double r, double g, double b);
  void SetCellLabelOpacity(double);
  void SetCellLabelFontFamily(int);
  void SetCellLabelBold(int);
  void SetCellLabelItalic(int);
  void SetCellLabelShadow(int);
  void SetCellLabelJustification(int);
  void SetCellLabelFontSize(int);
  void SetCellLabelFormat(const char*);

  //@{
  /**
   * Used to build the internal transform.
   */
  void SetOrientation(double, double, double);
  void SetOrigin(double, double, double);
  void SetPosition(double, double, double);
  void SetScale(double, double, double);
  void SetUserTransform(const double[16]);
  //@}

  /**
   * vtkAlgorithm::ProcessRequest() equivalent for rendering passes. This is
   * typically called by the vtkView to request meta-data from the
   * representations or ask them to perform certain tasks e.g.
   * PrepareForRendering.
   */
  int ProcessViewRequest(vtkInformationRequestKey* request_type, vtkInformation* inInfo,
    vtkInformation* outInfo) VTK_OVERRIDE;

protected:
  vtkDataLabelRepresentation();
  ~vtkDataLabelRepresentation();

  /**
   * Adds the representation to the view.  This is called from
   * vtkView::AddRepresentation().  Subclasses should override this method.
   * Returns true if the addition succeeds.
   */
  virtual bool AddToView(vtkView* view) VTK_OVERRIDE;

  /**
   * Removes the representation to the view.  This is called from
   * vtkView::RemoveRepresentation().  Subclasses should override this method.
   * Returns true if the removal succeeds.
   */
  virtual bool RemoveFromView(vtkView* view) VTK_OVERRIDE;

  /**
   * Fill input port information
   */
  virtual int FillInputPortInformation(int port, vtkInformation* info) VTK_OVERRIDE;

  /**
   * Subclasses should override this to connect inputs to the internal pipeline
   * as necessary. Since most representations are "meta-filters" (i.e. filters
   * containing other filters), you should create shallow copies of your input
   * before connecting to the internal pipeline. The convenience method
   * GetInternalOutputPort will create a cached shallow copy of a specified
   * input for you. The related helper functions GetInternalAnnotationOutputPort,
   * GetInternalSelectionOutputPort should be used to obtain a selection or
   * annotation port whose selections are localized for a particular input data object.
   */
  virtual int RequestData(
    vtkInformation*, vtkInformationVector**, vtkInformationVector*) VTK_OVERRIDE;

  /**
   * Overridden to check with the vtkPVCacheKeeper to see if the key is cached.
   */
  virtual bool IsCached(double cache_key) VTK_OVERRIDE;

  void UpdateTransform();

  vtkCompositeDataToUnstructuredGridFilter* MergeBlocks;
  vtkPVCacheKeeper* CacheKeeper;

  vtkLabeledDataMapper* PointLabelMapper;
  vtkTextProperty* PointLabelProperty;
  vtkActor2D* PointLabelActor;

  vtkCellCenters* CellCenters;
  vtkLabeledDataMapper* CellLabelMapper;
  vtkTextProperty* CellLabelProperty;
  vtkActor2D* CellLabelActor;

  vtkProp3D* TransformHelperProp;
  vtkTransform* Transform;

  vtkSmartPointer<vtkDataObject> Dataset;

  // Mutes label mapper warnings
  vtkCallbackCommand* WarningObserver;
  static void OnWarningEvent(vtkObject* source, unsigned long, void* clientdata, void*);

  int PointLabelVisibility;
  int CellLabelVisibility;

private:
  vtkDataLabelRepresentation(const vtkDataLabelRepresentation&) VTK_DELETE_FUNCTION;
  void operator=(const vtkDataLabelRepresentation&) VTK_DELETE_FUNCTION;
};

#endif
