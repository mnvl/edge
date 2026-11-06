# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# Translating SPIR-V shaders to GLSL ES for edge's OpenGL ES renderer (tools/shader_cross).
#
#   edge_gles_shaders(<out dir> <var> <shader.spv>...)
#
# makes the rules that write <name>.es.glsl and <name>.es.map into <out dir> for each <name>.spv, and sets <var> to what they make: a
# target depends on it.  The renderer finds the pair of a shader it is asked for by its .spv name (src/gles_renderer.h).

include(ExternalProject)

# An installed edge (edgeConfig.cmake) has the tool built already, bin/edge_shader_cross: EDGE_SHADER_CROSS_EXECUTABLE names it.
if(EDGE_SHADER_CROSS_EXECUTABLE)
  set(EDGE_SHADER_CROSS "${EDGE_SHADER_CROSS_EXECUTABLE}")
  set(_EDGE_SHADER_CROSS_TARGET)
  if(NOT EXISTS "${EDGE_SHADER_CROSS}")
    message(FATAL_ERROR "EdgeGles.cmake: no shader_cross at ${EDGE_SHADER_CROSS} (EDGE_SHADER_CROSS_EXECUTABLE)")
  endif()
else()
  # (where this file is, not where its includer is: a game includes it as edge's)
  set(_EDGE_SHADER_CROSS_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../tools/shader_cross")
  set(_EDGE_SHADER_CROSS_TARGET edge_shader_cross)

  # The tool is built for the machine that builds, also when the build is a cross one; built again with every build (BUILD_ALWAYS: a
  # changed tool is a new one, which makes every translation again).
  set(EDGE_SHADER_CROSS_DIR ${CMAKE_BINARY_DIR}/shader_cross_host)
  set(EDGE_SHADER_CROSS ${EDGE_SHADER_CROSS_DIR}/bin/shader_cross)
  # (on a Mac the SDK the build was given is the tool's too; not in a cross build, whose SDK is the target's: the host's is the one in
  # EDGE_HOST_OSX_SYSROOT where it is given (a Mac whose default macOS SDK cannot link), else the compiler's own)
  set(EDGE_HOST_OSX_SYSROOT "" CACHE PATH "a cross build on a Mac: the macOS SDK shader_cross is built with (default: the compiler's)")
  set(_EDGE_SHADER_CROSS_HOST_ARGS)
  if(APPLE AND CMAKE_OSX_SYSROOT AND NOT CMAKE_CROSSCOMPILING)
    list(APPEND _EDGE_SHADER_CROSS_HOST_ARGS -DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT})
  elseif(APPLE AND EDGE_HOST_OSX_SYSROOT)
    list(APPEND _EDGE_SHADER_CROSS_HOST_ARGS -DCMAKE_OSX_SYSROOT=${EDGE_HOST_OSX_SYSROOT})
  endif()
  if(NOT TARGET edge_shader_cross)
    ExternalProject_Add(edge_shader_cross
      SOURCE_DIR ${_EDGE_SHADER_CROSS_SOURCE}
      BINARY_DIR ${EDGE_SHADER_CROSS_DIR}
      CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${EDGE_SHADER_CROSS_DIR}/bin -DCMAKE_TOOLCHAIN_FILE= ${_EDGE_SHADER_CROSS_HOST_ARGS}
      BUILD_ALWAYS ON
      BUILD_BYPRODUCTS ${EDGE_SHADER_CROSS}
      INSTALL_COMMAND "")
  endif()
endif()

function(edge_gles_shaders OUT_DIR VAR)
  set(outputs)
  foreach(spv ${ARGN})
    get_filename_component(stem ${spv} NAME)
    string(REGEX REPLACE "\\.spv$" "" stem ${stem})
    set(glsl ${OUT_DIR}/${stem}.es.glsl)
    set(map ${OUT_DIR}/${stem}.es.map)
    add_custom_command(
      OUTPUT ${glsl} ${map}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${OUT_DIR}
      COMMAND ${EDGE_SHADER_CROSS} ${spv} ${glsl} ${map}
      DEPENDS ${spv} ${_EDGE_SHADER_CROSS_TARGET} ${EDGE_SHADER_CROSS}
      VERBATIM)
    list(APPEND outputs ${glsl} ${map})
  endforeach()
  set(${VAR} ${outputs} PARENT_SCOPE)
endfunction()
