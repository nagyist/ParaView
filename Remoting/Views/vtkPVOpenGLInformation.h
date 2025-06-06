// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause
/**
 * @class   vtkPVOpenGLInformation
 * @brief   Gets OpenGL information.
 *
 * Get details of OpenGL from the render server.
 */

#ifndef vtkPVOpenGLInformation_h
#define vtkPVOpenGLInformation_h

#include "vtkPVInformation.h"
#include "vtkRemotingViewsModule.h" //needed for exports

#include <string> // for string type

class VTKREMOTINGVIEWS_EXPORT vtkPVOpenGLInformation : public vtkPVInformation
{
public:
  static vtkPVOpenGLInformation* New();
  vtkTypeMacro(vtkPVOpenGLInformation, vtkPVInformation);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * Collects OpenGL information from the \c object. \c object must be
   * either a vtkPVView or a vtkRenderWindow. If not, this call will create a
   * vtkRenderWindow temporarily and use it to obtain OpenGL capabilities
   * information (see
   * vtkPVRenderingCapabilitiesInformation::NewOffscreenRenderWindow()).
   */
  void CopyFromObject(vtkObject* object) override;

  /**
   * Merge another information object.
   */
  void AddInformation(vtkPVInformation*) override;

  ///@{
  /**
   * Manage a serialized version of the information.
   */
  void CopyToStream(vtkClientServerStream*) override;
  void CopyFromStream(const vtkClientServerStream*) override;
  ///@}

  ///@{
  /**
   * Methods provide access to OpenGL information.
   */
  const std::string& GetVendor() const { return this->Vendor; }
  const std::string& GetVersion() const { return this->Version; }
  const std::string& GetRenderer() const { return this->Renderer; }
  const std::string& GetWindowBackend() const { return this->WindowBackend; }
  const std::string& GetCapabilities() const { return this->Capabilities; }
  ///@}

protected:
  vtkPVOpenGLInformation();
  ~vtkPVOpenGLInformation() override;

private:
  vtkPVOpenGLInformation(const vtkPVOpenGLInformation&) = delete;
  void operator=(const vtkPVOpenGLInformation&) = delete;

  std::string Vendor;
  std::string Version;
  std::string Renderer;
  std::string WindowBackend;
  std::string Capabilities;
};

#endif
