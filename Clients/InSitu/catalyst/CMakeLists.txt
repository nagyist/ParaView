# XXX(catalyst-static): Catalyst does not implement the logic to build and
# import plugins statically yet. Force shared libraries for now.
set(BUILD_SHARED_LIBS ON)

if (WIN32)
  set(catalyst_library_destination
    "${_vtk_build_RUNTIME_DESTINATION}/catalyst")
else ()
  set(catalyst_library_destination
    "${_vtk_build_LIBRARY_DESTINATION}/catalyst")
  file(RELATIVE_PATH _catalyst_impl_relpath
    "/prefix/${catalyst_library_destination}"
    "/prefix/${_vtk_build_LIBRARY_DESTINATION}")
  if (APPLE)
    set(catalyst_rel_rpath
      "@loader_path/${_catalyst_impl_relpath}")
  else ()
    set(catalyst_rel_rpath
      "$ORIGIN/${_catalyst_impl_relpath}")
  endif ()

  list(APPEND CMAKE_INSTALL_RPATH
    "${catalyst_rel_rpath}")
endif ()

# setup this property so other place in the code can use catalyst (c.f. Examples)
set_property(GLOBAL PROPERTY paraview_catalyst_directory "${catalyst_library_destination}")

catalyst_implementation(
  NAME    paraview
  TARGET  catalyst-paraview
  LIBRARY_DESTINATION
    "${catalyst_library_destination}"
  SOURCES
    ParaViewCatalyst.cxx
    vtkCatalystBlueprint.cxx
    vtkCatalystBlueprint.h
  CATALYST_TARGET VTK::catalyst)
add_library(ParaView::catalyst-paraview ALIAS catalyst-paraview)

target_link_libraries(catalyst-paraview
  PRIVATE
    ParaView::InSitu
    ParaView::VTKExtensionsCore
    VTK::IOCatalystConduit
    VTK::FiltersCore
    ParaView::RemotingServerManager)

_vtk_module_optional_dependency_exists(VTK::ParallelMPI
  SATISFIED_VAR _have_vtk_parallelmpi)
_vtk_module_optional_dependency_exists(VTK::IOIOSS
  SATISFIED_VAR _have_vtk_ioioss)
_vtk_module_optional_dependency_exists(VTK::IOFides
  SATISFIED_VAR _have_vtk_iofides)

if (_have_vtk_parallelmpi)
  target_link_libraries(catalyst-paraview
    PRIVATE
      VTK::ParallelMPI)
endif ()

if (_have_vtk_ioioss)
  target_link_libraries(catalyst-paraview
    PRIVATE
      VTK::IOIOSS)
endif ()

if (_have_vtk_iofides)
  target_link_libraries(catalyst-paraview
    PRIVATE
      VTK::IOFides)
endif ()

target_compile_definitions(catalyst-paraview
  PRIVATE
    "VTK_MODULE_ENABLE_VTK_ParallelMPI=$<BOOL:${_have_vtk_parallelmpi}>"
    "VTK_MODULE_ENABLE_VTK_IOIOSS=$<BOOL:${_have_vtk_ioioss}>"
    "VTK_MODULE_ENABLE_VTK_IOFides=$<BOOL:${_have_vtk_iofides}>")

_vtk_module_optional_dependency_exists(VTK::Python
  SATISFIED_VAR _have_vtk_python)

if (_have_vtk_python)
  # Simulations loading the ParaView Catalyst implementation might not have
  # their own Python, so we need to ensure that it is brought in when loaded by
  # libcatalyst.
  set_property(TARGET catalyst-paraview
    PROPERTY
      INTERFACE_vtk_python_direct_link 1)
  target_link_libraries(catalyst-paraview
    PRIVATE
      VTK::PythonUsed)
endif ()

# Clear the `-pvVERSION` suffix (if any).
set(_vtk_build_LIBRARY_NAME_SUFFIX "")
# Clear version information.
set(_vtk_build_VERSION "")
set(_vtk_build_SOVERSION "")

_vtk_module_apply_properties(catalyst-paraview)
