// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkInitializationHelper.h"
#include "vtkNew.h"
#include "vtkProcessModule.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMSession.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMSourceProxy.h"
#include "vtkSmartPointer.h"
#include "vtkSphereSource.h"
#include "vtkWeakPointer.h"

extern int TestRecreateVTKObjects(int argc, char* argv[])
{
  (void)argc;

  vtkInitializationHelper::Initialize(argv[0], vtkProcessModule::PROCESS_CLIENT);

  // Create a new session.
  vtkNew<vtkSMSession> session;
  vtkSMSessionProxyManager* pxm = session->GetSessionProxyManager();

  vtkSmartPointer<vtkSMSourceProxy> sphereSource;
  sphereSource.TakeReference(
    vtkSMSourceProxy::SafeDownCast(pxm->NewProxy("sources", "SphereSource")));
  vtkSMPropertyHelper(sphereSource, "Radius").Set(10);
  sphereSource->UpdateVTKObjects();
  sphereSource->UpdatePipeline();

  vtkWeakPointer<vtkObject> oldObject =
    vtkObject::SafeDownCast(sphereSource->GetClientSideObject());
  sphereSource->RecreateVTKObjects();

  int exitCode = EXIT_SUCCESS;
  try
  {
    if (oldObject != nullptr)
    {
      throw "ERROR: Old VTKObject not deleted!!!";
    }
    if (sphereSource->GetClientSideObject() == nullptr)
    {
      throw "ERROR: New VTKObject not created!!!";
    }

    // Ensure that the new VTK object indeed has the same radius as before.
    if (vtkSphereSource::SafeDownCast(sphereSource->GetClientSideObject())->GetRadius() != 10)
    {
      throw "ERROR: Recreated VTK object doesn't have same state as original!!!";
    }
  }
  catch (const char* msg)
  {
    cerr << msg << endl;
  }
  vtkInitializationHelper::Finalize();
  return exitCode;
}
