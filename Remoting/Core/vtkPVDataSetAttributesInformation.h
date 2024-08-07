// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause
/**
 * @class   vtkPVDataSetAttributesInformation
 * @brief   List of array info
 *
 * Information associated with vtkDataSetAttributes object (i.e point data).
 * This object does not have any user interface.  It is created and destroyed
 * on the fly as needed.  It may be possible to add features of this object
 * to vtkDataSetAttributes.  That would eliminate all of the "Information"
 * in ParaView.
 */

#ifndef vtkPVDataSetAttributesInformation_h
#define vtkPVDataSetAttributesInformation_h

#include "vtkObject.h"
#include "vtkRemotingCoreModule.h" //needed for exports

#include <memory> // For unique_ptr

class vtkClientServerStream;
class vtkDataObject;
class vtkPVArrayInformation;

class VTKREMOTINGCORE_EXPORT vtkPVDataSetAttributesInformation : public vtkObject
{
public:
  /**
   * @brief Iterator over a `vtkPVDataSetAttributesInformation`'s arrays; yielding their
   * `vtkPVArrayInformation`. This iterator is slower thant looping over arrays using
   * `GetNumberOfArrays()` and `GetArrayInformation(idx)` but returns the arrays ordered by name.
   * Prefer the other approach unless you explicitely need the ordered arrays.
   */
  class VTKREMOTINGCORE_EXPORT AlphabeticalArrayInformationIterator
  {
  public:
    AlphabeticalArrayInformationIterator(vtkPVDataSetAttributesInformation* source);
    ~AlphabeticalArrayInformationIterator();

    void GoToFirstItem();
    void GoToNextItem();
    bool IsDoneWithTraversal() const;
    vtkPVArrayInformation* GetCurrentArrayInformation();

  private:
    struct vtkInternals;
    std::unique_ptr<vtkInternals> Internals;
  };

  static vtkPVDataSetAttributesInformation* New();
  vtkTypeMacro(vtkPVDataSetAttributesInformation, vtkObject);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  ///@{
  /**
   * Returns the field association to which the instance corresponds to.
   * Returned value can be vtkDataObject::POINT, vtkDataObject::CELL,
   * vtkDataObject::FIELD, etc i.e. vtkDataObject::FieldAssociations or
   * vtkDataObject::AttributeTypes.
   */
  vtkGetMacro(FieldAssociation, int);
  ///@}

  /**
   * Initialize this instances to its default state.
   */
  void Initialize();

  /**
   * Returns the number of array informations available in this instance.
   */
  int GetNumberOfArrays() const;

  /**
   * Returns the maximum number of tuples in all known arrays.
   */
  int GetMaximumNumberOfTuples() const;

  ///@{
  /**
   * Returns array information for the chosen array either by name or by index.
   * Note that the index overload is significantly is faster.
   */
  vtkPVArrayInformation* GetArrayInformation(int idx) const;
  vtkPVArrayInformation* GetArrayInformation(const char* name) const;
  ///@}

  /**
   * Returns array information for an array associated with a specific attribute
   * type. Supported attribute types are `vtkDataSetAttributes::AttributeTypes`.
   */
  vtkPVArrayInformation* GetAttributeInformation(int attributeType);

  /**
   * Returns the attribute type if at the array at the given index is an
   * attribute.
   */
  int IsArrayAnAttribute(int arrayIndex);

  /**
   * @brief Returns a new iterator to traverse array informations alphabetically.
   * This approach is noticeably slower than looping over array indices. Prefer doing so
   * unless you explicitely need the ordered arrays.
   * Caller is responsible for releasing the iterator memory.
   */
  AlphabeticalArrayInformationIterator* NewAlphabeticalArrayInformationIterator();

protected:
  vtkPVDataSetAttributesInformation();
  ~vtkPVDataSetAttributesInformation() override;

  friend class vtkPVDataInformation;
  friend class vtkPVDataInformationAccumulator;

  /**
   * Set field association.
   */
  vtkSetMacro(FieldAssociation, int);

  ///@{
  /**
   * Manage a serialized version of the information.
   */
  void CopyToStream(vtkClientServerStream*);
  void CopyFromStream(const vtkClientServerStream*);
  ///@}

  /**
   * Combine with another vtkPVDataSetAttributesInformation instance.
   */
  void AddInformation(vtkPVDataSetAttributesInformation*);

  /**
   * Copies from another vtkPVDataSetAttributesInformation instance.
   */
  void DeepCopy(vtkPVDataSetAttributesInformation*);

  /**
   * Initializes this instance using the data object.
   */
  void CopyFromDataObject(vtkDataObject* dobj);

private:
  vtkPVDataSetAttributesInformation(const vtkPVDataSetAttributesInformation&) = delete;
  void operator=(const vtkPVDataSetAttributesInformation&) = delete;

  int FieldAssociation;

  class vtkInternals;
  vtkInternals* Internals;
};

#endif
