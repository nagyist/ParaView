set(_ParaViewTranslations_cmake_dir "${CMAKE_CURRENT_LIST_DIR}")

#[==[
@brief Generate a C++ header with input XML labels and UI strings that can be parsed by
Qt Linguist tools. It is using a python utility at
Utilities/Localization/XML_translations_header_generator.py

~~~
paraview_generate_translation_header(
  TARGET         <name>
  INPUT_FILES    <file>...
  RESULT_FILE    <file>)
~~~

  * `TARGET`: (Required) The name of the target that generate the translation
    header file.
  * `INPUT_FILES`: (Required) The absolute path of the input files.
  * `RESULT_FILE`: (Required) The absolute path of the desired result file.
#]==]
function(paraview_generate_translation_header)
  cmake_parse_arguments(PARSE_ARGV 0 _pv_generate_tr_h
    ""
    "TARGET;RESULT_FILE"
    "INPUT_FILES")
  if (_pv_generate_tr_h_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "Unrecognized arguments for paraview_generate_translation_header: "
      "${_pv_generate_tr_h_UNPARSED_ARGUMENTS}.")
  endif ()
  if (NOT DEFINED _pv_generate_tr_h_RESULT_FILE)
    message(FATAL_ERROR
      "The `RESULT_FILE` argument is required.")
  endif ()
  if (NOT DEFINED _pv_generate_tr_h_INPUT_FILES)
    message(FATAL_ERROR
      "The `INPUT_FILES` argument is required.")
  endif ()
  if (NOT DEFINED _pv_generate_tr_h_TARGET)
    message(FATAL_ERROR
      "The `TARGET` argument is required.")
  endif ()
  find_package(Python3 QUIET REQUIRED COMPONENTS Interpreter)
  add_custom_command(OUTPUT ${_pv_generate_tr_h_RESULT_FILE}
    COMMAND "$<TARGET_FILE:Python3::Interpreter>" "${_ParaViewTranslations_cmake_dir}/XML_translations_header_generator.py" -o "${_pv_generate_tr_h_RESULT_FILE}" ${_pv_generate_tr_h_INPUT_FILES} -s "${CMAKE_SOURCE_DIR}/")
  add_custom_target("${_pv_generate_tr_h_TARGET}"
    DEPENDS "${_pv_generate_tr_h_RESULT_FILE}")
endfunction()

#[==[
@brief Generate a Qt translation source file from the given source files

~~~
paraview_create_translation(
  TARGET              <name>
  INPUT_FILES         <files...>
  OUTPUT_TS           <file>
~~~

  * `TARGET`: (Required) The name of the target that generate the translation
    source file.
  * `INPUT_FILES`: (Required) The source files to search translatable strings in.
  * `OUTPUT_TS`: (Required) The absolute path of the desired result file.
#]==]
function(paraview_create_translation)
  cmake_parse_arguments(PARSE_ARGV 0 _pv_create_tr
    ""
    "TARGET;OUTPUT_TS"
    "FILES")
  if (_pv_create_tr_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "Unrecognized arguments for paraview_create_translation: "
      "${_pv_create_tr_UNPARSED_ARGUMENTS}.")
  endif ()
  if (NOT DEFINED _pv_create_tr_TARGET)
    message(FATAL_ERROR
      "The `TARGET` argument is required.")
  endif ()
  if (NOT DEFINED _pv_create_tr_FILES)
    message(FATAL_ERROR
      "The `FILES` argument is required.")
  endif ()
  if (NOT DEFINED _pv_create_tr_OUTPUT_TS)
    message(FATAL_ERROR
      "The `OUTPUT_TS` argument is required.")
  endif ()

  # Transforms all relative paths in the provided
  # FILES by their absolute paths.
  set(_pv_create_tr_files)
  foreach (_pv_create_tr_in_file IN LISTS _pv_create_tr_FILES)
    if (NOT IS_ABSOLUTE "${_pv_create_tr_in_file}")
      string(PREPEND _pv_create_tr_in_file
        "${CMAKE_CURRENT_SOURCE_DIR}/")
    endif ()
    list(APPEND _pv_create_tr_files ${_pv_create_tr_in_file})
  endforeach ()
  find_package(Qt5 REQUIRED QUIET COMPONENTS LinguistTools)
  get_filename_component(_pv_create_tr_directory ${_pv_create_tr_OUTPUT_TS} DIRECTORY)
  file(MAKE_DIRECTORY "${_pv_create_tr_directory}")
  add_custom_command(
    OUTPUT  "${_pv_create_tr_OUTPUT_TS}"
    COMMAND "$<TARGET_FILE:Qt5::lupdate>" ${_pv_create_tr_files} -ts "${_pv_create_tr_OUTPUT_TS}"
    DEPENDS ${_pv_create_tr_files})
  add_custom_target("${_pv_create_tr_TARGET}"
    DEPENDS "${_pv_create_tr_OUTPUT_TS}")
endfunction()
