# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# Translating SPIR-V shaders to Metal for edge's Metal renderer (tools/shader_cross --msl).
#
#   edge_metal_shaders(<out dir> <var> <sdk> <shader.spv>...)
#
# makes the rules that write <name>.metal and <name>.msl.map (what the renderer binds, shader_cross.cc says the lines) and, from the
# MSL, <name>.<sdk>.metallib for each <name>.spv, into <out dir>; <var> is set to what they make: a target depends on it.  <sdk> is
# what `xcrun -sdk` takes: macosx, iphoneos or iphonesimulator.  A library for each shader, not one for all: SPIRV-Cross names every
# entry point main0.  The renderer finds the pair of a shader it is asked for by its .spv name (src/metal_renderer.h).

include(${CMAKE_CURRENT_LIST_DIR}/EdgeGles.cmake)       # (the tool is the same one: its ExternalProject, EDGE_SHADER_CROSS)

find_program(EDGE_XCRUN xcrun)

# The language the libraries are written in is MSL 2.4 (shader_cross --msl says so: iOS 15 and macOS 12 on) and they run on the oldest OS the
# build is for (CMAKE_OSX_DEPLOYMENT_TARGET): without these the compiler of Xcode 26 makes MSL 4.0, which a library cannot be loaded from
# on an older OS ("This library is using language version 4.0 which is not supported on this OS").
function(_edge_metal_flags SDK VAR)
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    message(FATAL_ERROR "edge_metal_shaders: no CMAKE_OSX_DEPLOYMENT_TARGET: the libraries must name the oldest OS they run on")
  endif()
  if(SDK STREQUAL "macosx")
    set(${VAR} -std=macos-metal2.4 -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET} PARENT_SCOPE)
  elseif(SDK STREQUAL "iphoneos")
    set(${VAR} -std=ios-metal2.4 -mios-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET} PARENT_SCOPE)
  else()
    set(${VAR} -std=ios-metal2.4 -mios-simulator-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET} PARENT_SCOPE)
  endif()
endfunction()

function(edge_metal_shaders OUT_DIR VAR SDK)
  if(NOT SDK MATCHES "^(macosx|iphoneos|iphonesimulator)$")
    message(FATAL_ERROR "edge_metal_shaders: the sdk is macosx, iphoneos or iphonesimulator, not '${SDK}'")
  endif()
  if(NOT EDGE_XCRUN)
    message(FATAL_ERROR "edge_metal_shaders: xcrun (Xcode) is needed to compile Metal shaders")
  endif()
  _edge_metal_flags(${SDK} flags)
  set(outputs)
  foreach(spv ${ARGN})
    get_filename_component(stem ${spv} NAME)
    string(REGEX REPLACE "\\.spv$" "" stem ${stem})
    set(metal ${OUT_DIR}/${stem}.metal)
    set(map ${OUT_DIR}/${stem}.msl.map)
    set(air ${OUT_DIR}/${stem}.${SDK}.air)
    set(lib ${OUT_DIR}/${stem}.${SDK}.metallib)
    add_custom_command(
      OUTPUT ${metal} ${map}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${OUT_DIR}
      COMMAND ${EDGE_SHADER_CROSS} --msl ${spv} ${metal} ${map}
      DEPENDS ${spv} ${_EDGE_SHADER_CROSS_TARGET} ${EDGE_SHADER_CROSS}
      VERBATIM)
    add_custom_command(
      OUTPUT ${lib}
      COMMAND ${EDGE_XCRUN} -sdk ${SDK} metal ${flags} -c ${metal} -o ${air}
      COMMAND ${EDGE_XCRUN} -sdk ${SDK} metallib ${air} -o ${lib}
      DEPENDS ${metal}
      VERBATIM)
    list(APPEND outputs ${metal} ${map} ${lib})
  endforeach()
  set(${VAR} ${outputs} PARENT_SCOPE)
endfunction()
