// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
/**
 * @class   vtkPGenericIOMultiBlockWriter
 *
 */

#ifndef vtkPGenericIOMultiBlockWriter_h
#define vtkPGenericIOMultiBlockWriter_h

#include "vtkPVVTKExtensionsCosmoToolsModule.h" // for export macro
#include "vtkWriter.h"

class vtkMultiProcessController;

class VTKPVVTKEXTENSIONSCOSMOTOOLS_EXPORT vtkPGenericIOMultiBlockWriter : public vtkWriter
{
public:
  static vtkPGenericIOMultiBlockWriter* New();
  vtkTypeMacro(vtkPGenericIOMultiBlockWriter, vtkWriter);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkSetStringMacro(FileName);
  vtkGetStringMacro(FileName);

protected:
  vtkPGenericIOMultiBlockWriter();
  ~vtkPGenericIOMultiBlockWriter();

  int FillInputPortInformation(int port, vtkInformation* info) override;
  virtual void WriteData();

private:
  class vtkInternals;
  vtkInternals* Internals;
  char* FileName;
  vtkMultiProcessController* Controller;
  vtkPGenericIOMultiBlockWriter(const vtkPGenericIOMultiBlockWriter&) = delete;
  void operator=(const vtkPGenericIOMultiBlockWriter&) = delete;
};

#endif
