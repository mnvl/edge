# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# What edge reads at run time, beside a game's binary.  edge's CMakeLists includes this file, and an installed edge's package
# (edgeConfig.cmake: the shaders from share/edge/shaders); a project calls the function after add_subdirectory(edge) or
# find_package(edge).
#
#   edge_bundle_runtime(<target> DESTINATION <dir>)
#
# <dir>/shaders/edge: edge's own shaders (SPIR-V, and their GLSL ES translations when edge has its GLES backend); on macOS
# <dir>/vulkan: the Vulkan driver, MoltenVK, and a manifest that names it there (a bare name would send the loader searching the
# system for it).  Elsewhere the system's Vulkan loader finds the system's driver and nothing of it is copied.  At run time
# edge::use_bundled_runtime(<dir>) (bundled_runtime.h) points edge and the loader at them.  <target> copies them, with every
# build (ALL).

include_guard(GLOBAL)

function(edge_bundle_runtime TARGET)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "DESTINATION" "")
  if(NOT ARG_DESTINATION)
    message(FATAL_ERROR "edge_bundle_runtime(${TARGET}): DESTINATION is needed")
  endif()
  if(ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "edge_bundle_runtime(${TARGET}): unknown arguments ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT EDGE_RUNTIME_SHADERS)
    message(FATAL_ERROR "edge_bundle_runtime(${TARGET}): edge built no shaders (EDGE_RUNTIME_SHADERS is empty: Vulkan is off)")
  endif()
  set(outputs)
  foreach(shader ${EDGE_RUNTIME_SHADERS})
    get_filename_component(name "${shader}" NAME)
    set(copy "${ARG_DESTINATION}/shaders/edge/${name}")
    add_custom_command(
      OUTPUT "${copy}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARG_DESTINATION}/shaders/edge"
      COMMAND "${CMAKE_COMMAND}" -E copy "${shader}" "${copy}"
      DEPENDS "${shader}" ${EDGE_RUNTIME_SHADER_TARGETS}
      VERBATIM)
    list(APPEND outputs "${copy}")
  endforeach()
  if(APPLE)
    find_library(EDGE_MOLTENVK_LIBRARY MoltenVK)
    if(NOT EDGE_MOLTENVK_LIBRARY)
      message(FATAL_ERROR "edge_bundle_runtime(${TARGET}): MoltenVK is not found (EDGE_MOLTENVK_LIBRARY): it is the Vulkan driver on macOS")
    endif()
    set(manifest "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_MoltenVK_icd.json")
    file(WRITE "${manifest}"
      "{\n  \"file_format_version\": \"1.0.0\",\n  \"ICD\": {\n    \"library_path\": \"./libMoltenVK.dylib\",\n"
      "    \"api_version\": \"1.4.0\",\n    \"is_portability_driver\": true\n  }\n}\n")
    add_custom_command(
      OUTPUT "${ARG_DESTINATION}/vulkan/libMoltenVK.dylib" "${ARG_DESTINATION}/vulkan/MoltenVK_icd.json"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARG_DESTINATION}/vulkan"
      COMMAND "${CMAKE_COMMAND}" -E copy "${EDGE_MOLTENVK_LIBRARY}" "${ARG_DESTINATION}/vulkan/libMoltenVK.dylib"
      COMMAND "${CMAKE_COMMAND}" -E copy "${manifest}" "${ARG_DESTINATION}/vulkan/MoltenVK_icd.json"
      DEPENDS "${EDGE_MOLTENVK_LIBRARY}" "${manifest}"
      VERBATIM)
    list(APPEND outputs "${ARG_DESTINATION}/vulkan/libMoltenVK.dylib" "${ARG_DESTINATION}/vulkan/MoltenVK_icd.json")
  endif()
  add_custom_target(${TARGET} ALL DEPENDS ${outputs})
endfunction()
