# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# Shaders drawn through edge: GLSL that includes edge.glsl, compiled to SPIR-V (and, where edge builds its OpenGL ES backend, translated to
# GLSL ES beside it).  edge's CMakeLists includes this file, and an installed edge's package (edgeConfig.cmake: edge.glsl in
# share/edge/glsl); a project calls the function after add_subdirectory(edge) or find_package(edge).
#
#   edge_add_shaders(<target> DESTINATION <dir> SOURCES <file>... [DEPENDS <file>...])
#
# Each source is <name>.vert or <name>.frag, the stage glslangValidator takes from the extension; it becomes <dir>/<name>.<stage>.spv
# (and <name>.<stage>.es.glsl with its .es.map for the GLES renderer, <name>.<stage>.metal with its .msl.map and .<sdk>.metallib for the Metal one), compiled for Vulkan 1.0 with -I of edge.glsl's directory and of
# the source's own.  DEPENDS: the files they include (their own .glsl), which rebuild them when they change.  <target> builds them all,
# with every build (ALL); the variable <target>_OUTPUTS names what it makes.

include_guard(GLOBAL)

set(EDGE_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "edge's CMake helpers")
find_program(GLSLANG_VALIDATOR glslangValidator)

function(edge_add_shaders TARGET)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "DESTINATION" "SOURCES;DEPENDS")
  if(NOT ARG_DESTINATION OR NOT ARG_SOURCES)
    message(FATAL_ERROR "edge_add_shaders(${TARGET}): DESTINATION and SOURCES are needed")
  endif()
  if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "edge_add_shaders(${TARGET}): glslangValidator is needed to build shaders")
  endif()
  if(NOT EXISTS "${EDGE_SHADER_INCLUDE_DIR}/edge.glsl")
    message(FATAL_ERROR "edge_add_shaders(${TARGET}): no edge.glsl in EDGE_SHADER_INCLUDE_DIR ('${EDGE_SHADER_INCLUDE_DIR}')")
  endif()
  # edge.glsl and what a shader may include beside it (edge_brdf, edge_material, edge_skin)
  file(GLOB EDGE_SHADER_INCLUDES ${EDGE_SHADER_INCLUDE_DIR}/edge*.glsl)
  set(outputs)
  foreach(source ${ARG_SOURCES})
    get_filename_component(source ${source} ABSOLUTE)
    get_filename_component(name ${source} NAME)
    get_filename_component(dir ${source} DIRECTORY)
    if(NOT name MATCHES "\\.(vert|frag)$")
      message(FATAL_ERROR "edge_add_shaders(${TARGET}): ${source} is not a .vert or a .frag")
    endif()
    set(binary ${ARG_DESTINATION}/${name}.spv)
    add_custom_command(
      OUTPUT ${binary}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${ARG_DESTINATION}
      COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.0 -I${EDGE_SHADER_INCLUDE_DIR} -I${dir} ${source} -o ${binary}
      DEPENDS ${source} ${ARG_DEPENDS} ${EDGE_SHADER_INCLUDES}
      VERBATIM)
    list(APPEND outputs ${binary})
  endforeach()
  set(spirv_outputs ${outputs})
  if(EDGE_USE_GLES_SHADERS)
    include(${EDGE_CMAKE_DIR}/EdgeGles.cmake)
    edge_gles_shaders(${ARG_DESTINATION} translations ${outputs})
    list(APPEND outputs ${translations})
  endif()
  if(EDGE_USE_METAL_SHADERS)
    include(${EDGE_CMAKE_DIR}/EdgeMetal.cmake)
    edge_metal_shaders(${ARG_DESTINATION} metal_outputs ${EDGE_METAL_SDK} ${spirv_outputs})
    list(APPEND outputs ${metal_outputs})
  endif()
  add_custom_target(${TARGET} ALL DEPENDS ${outputs})
  set(${TARGET}_OUTPUTS ${outputs} PARENT_SCOPE)
endfunction()
