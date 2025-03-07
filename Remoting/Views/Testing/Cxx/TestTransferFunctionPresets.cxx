// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkInitializationHelper.h"
#include "vtkNew.h"
#include "vtkPVXMLElement.h"
#include "vtkProcessModule.h"
#include "vtkSMTransferFunctionPresets.h"
#include "vtkSmartPointer.h"
#include "vtkTestUtilities.h"

#include "vtk_jsoncpp.h"
#include <cassert>
#include <sstream>

#define myassert(condition, message)                                                               \
  if ((condition))                                                                                 \
  {                                                                                                \
    cout << (message) << " -- SUCCESS" << endl;                                                    \
  }                                                                                                \
  else                                                                                             \
  {                                                                                                \
    cout << (message) << " -- FAILED" << endl;                                                     \
    return EXIT_FAILURE;                                                                           \
  }

extern int TestTransferFunctionPresets(int argc, char* argv[])
{
  (void)argc;

  vtkInitializationHelper::Initialize(argv[0], vtkProcessModule::PROCESS_CLIENT);

  auto presets = vtkSMTransferFunctionPresets::GetInstance();

  myassert(presets->GetNumberOfPresets() > 0, "Load presets");
  cout << "Number of presets: " << presets->GetNumberOfPresets() << endl;

  unsigned int testPreset = 10;

  myassert(presets->GetPresetAsString(testPreset).empty() == false, "Has test preset");
  cout << "Preset: " << endl << presets->GetPresetAsString(testPreset);

  unsigned int old_size = presets->GetNumberOfPresets();

  // Test that "builtin" preset cannot be removed.
  myassert(presets->RemovePreset(testPreset) == false, "Cannot remove builtin preset");
  myassert(presets->GetNumberOfPresets() == old_size, "Preset size is unchanged");

  // Test that "builtin" preset can be overridden.
  Json::Value preset = presets->GetPreset(testPreset);
  preset["ColorSpace"] = "Bogus";
  presets->AddPreset("Bogus", preset);
  /*

  myassert(presets->GetPreset(testPreset)["ColorSpace"].asString() == "Bogus", "Overridden preset is
  of right type");

  // create new instance and verify that our overridden preset survived.
  presets = vtkSmartPointer<vtkSMTransferFunctionPresets>::New();
  myassert(presets->GetPresetAsString(testPreset).empty() == false, "Has overridden preset on
  reload");
  cout << "Preset: " << endl
       << presets->GetPresetAsString(testPreset);
  myassert(presets->GetPreset(testPreset)["ColorSpace"].asString() == "Bogus", "Overridden preset is
  of right type");
  myassert(presets->RemovePreset(testPreset) == true, "Can remove custom preset after reload");

  // Now we'd expect to get the default preset.
  presets = vtkSmartPointer<vtkSMTransferFunctionPresets>::New();
  myassert(presets->GetPresetAsString(testPreset).empty() == false, "Can restore builtin preset");

  // create new instance and verify that our overridden preset survived.
  presets = vtkSmartPointer<vtkSMTransferFunctionPresets>::New();
  myassert(presets->GetPresetAsString(testPreset) != nullptr, "Has test preset");
  myassert(presets->RemovePreset(testPreset) == false, "Cannot remove builtin preset");
  */

  old_size = presets->GetNumberOfPresets();
  char* fname = vtkTestUtilities::ExpandDataFileName(argc, argv, "Testing/Data/RdPu.ct");
  presets->ImportPresets(fname);
  myassert(presets->GetNumberOfPresets() == old_size + 1, "Preset size is incremented");
  preset = presets->GetFirstPresetWithName("RdPu");
  myassert(!preset.empty(), "Import of Visit preset should succeed");

  delete[] fname;

  presets = nullptr;

  vtkInitializationHelper::Finalize();
  return EXIT_SUCCESS;
}
