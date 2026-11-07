# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

CMAKE_MINIMUM_REQUIRED(VERSION 3.20)
# Built by itself on a Mac (its tests): Apple silicon, macOS 13.3 on.  (A parent's PROJECT is before this.)
IF(APPLE AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  IF(NOT CMAKE_OSX_ARCHITECTURES)
    SET(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Apple silicon only" FORCE)
  ENDIF()
  IF(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    SET(CMAKE_OSX_DEPLOYMENT_TARGET 13.3 CACHE STRING "the oldest macOS edge runs on" FORCE)
  ENDIF()
ENDIF()
PROJECT(EDGE VERSION 3.0.1)
# The oldest Apple systems edge builds for: the log's std::format formats a float through std::to_chars, which Apple's C++ library
# has from macOS 13.3 and iOS 16.3 on (older targets fail to compile it, "unavailable").  Said here, at configure, rather than there.
IF(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
  IF(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    SET(EDGE_OLDEST_APPLE_OS 16.3)
  ELSE()
    SET(EDGE_OLDEST_APPLE_OS 13.3)
  ENDIF()
  IF(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS EDGE_OLDEST_APPLE_OS)
    MESSAGE(FATAL_ERROR "CMAKE_OSX_DEPLOYMENT_TARGET ${CMAKE_OSX_DEPLOYMENT_TARGET}: edge needs ${EDGE_OLDEST_APPLE_OS} or newer on ${CMAKE_SYSTEM_NAME}")
  ENDIF()
ENDIF()

# (for edge and what it fetches; a parent's targets choose their own)
SET(CMAKE_CXX_STANDARD 20)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_EXTENSIONS ON)

SET(EDGE_ROOT ${EDGE_SOURCE_DIR})
SET(EDGE_CMAKE_DIR "${EDGE_ROOT}/etc/cmake")
LIST(APPEND CMAKE_MODULE_PATH "${EDGE_CMAKE_DIR}")
INCLUDE(GNUInstallDirs)

# (on where edge is built by itself; a parent that adds edge asks for what it uses)
IF(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  SET(EDGE_TOP_LEVEL ON)
ELSE()
  SET(EDGE_TOP_LEVEL OFF)
ENDIF()

# `cmake --install`: the libraries, their headers as <edge/x.h>, edge's shaders, the tools, the CMake helpers and a CMake package
# (find_package(edge CONFIG): edge::edge, edge::sdl, edge::imgui, edge::asset, edge::convert, edge::shot).  Off for a wheel, which
# installs pyedge's package instead.  A parent that turns it on turns rove's ROVE_INSTALL on too: edge's package needs rove's.
OPTION(EDGE_INSTALL "Generate edge's install rules and CMake package" ${EDGE_TOP_LEVEL})
IF(SKBUILD)
  SET(EDGE_INSTALL OFF)
ENDIF()


IF(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR AND NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  SET(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)." FORCE)
ENDIF()

# pyedge, and rove's Python bindings with it. A C++-only consumer turns it
# off and needs neither Python nor nanobind to build.
OPTION(EDGE_PYTHON "Build the pyedge Python module." ON)

# test_edge and its ctest registrations; a cross build (the phone's) cannot run them.  Boost.Test is the one thing of Boost edge needs.
OPTION(EDGE_TESTS "Build the test_edge executable (on when edge is built by itself)." ${EDGE_TOP_LEVEL})

IF(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  # Standalone build — use edge's own binary dir.
  SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
  SET(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)
ELSE()
  # Subdirectory build — use parent's output dirs so all .so files land together.
  SET(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
  SET(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib)
ENDIF()

ENABLE_TESTING()

# Vulkan where it is there (src/CMakeLists.txt builds the backend with it): Vulkan::Vulkan.
FIND_PACKAGE(Vulkan)
SET(EDGE_USE_VULKAN ${Vulkan_FOUND})

# The OpenGL ES backend (src/gles_*): off unless asked for.  The host makes the context and gives it to the
# renderer (GLESContext, as it makes a Vulkan surface); libGLESv3 (or v2, which exports the same ES 3.0 entry points on the desktop) is the
# API.  Asked for and not there is an error, not a build without it.
OPTION(EDGE_GLES "Build the OpenGL ES renderer (needs GLES3/gl3.h and libGLESv3)." OFF)
# edge_sdl, the part of edge that needs SDL3 (edge itself links none): an SDL window's Vulkan surface (vulkan_sdl_surface.h) and a GLES
# context made with SDL (gles_sdl_context.h), for a host that uses SDL and for the tests.  On with the GLES backend, whose tests need it.
# (on where edge is built by itself, for its tests; a parent asks for what it uses)
IF(EDGE_TOP_LEVEL OR EDGE_GLES OR EDGE_METAL)
  SET(EDGE_SDL_DEFAULT ON)
ELSE()
  SET(EDGE_SDL_DEFAULT OFF)
ENDIF()
OPTION(EDGE_SDL "Build edge_sdl (SDL3): an SDL window's Vulkan surface, a GLES context made with SDL." ${EDGE_SDL_DEFAULT})
IF(EDGE_SDL AND NOT TARGET SDL3::SDL3)
  FIND_PACKAGE(SDL3 REQUIRED CONFIG)
ENDIF()
# edge_imgui, Dear ImGui drawn through edge (imgui_renderer.h, a header compiled against the program's ImGui, 1.92 or newer): it
# links the parent's target imgui, made before edge is added, which has ImGui's core; edge built by itself fetches one for its tests.
OPTION(EDGE_IMGUI "Build edge_imgui: Dear ImGui drawn through edge (the parent's imgui target)." ${EDGE_TOP_LEVEL})
IF(EDGE_GLES AND EMSCRIPTEN)
  # a browser's WebGL 2, which is OpenGL ES 3.0: Emscripten's own GLES3/gl3.h, and its GL library asked for at the link (no library to find)
  SET(EDGE_USE_GLES TRUE)
  ADD_LIBRARY(edge_gles_api INTERFACE)
  SET_TARGET_PROPERTIES(edge_gles_api PROPERTIES EXPORT_NAME gles_api)
  TARGET_LINK_OPTIONS(edge_gles_api INTERFACE -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2)
  MESSAGE(STATUS "OpenGL ES backend: WebGL 2")
ELSEIF(EDGE_GLES)
  FIND_LIBRARY(EDGE_GLES_LIBRARY NAMES GLESv3 GLESv2)
  FIND_PATH(EDGE_GLES_INCLUDE_DIR GLES3/gl3.h)
  IF(NOT EDGE_GLES_LIBRARY OR NOT EDGE_GLES_INCLUDE_DIR)
    MESSAGE(FATAL_ERROR "EDGE_GLES needs GLES3/gl3.h and libGLESv3 (or libGLESv2)")
  ENDIF()
  SET(EDGE_USE_GLES TRUE)
  ADD_LIBRARY(edge_gles_api INTERFACE)
  SET_TARGET_PROPERTIES(edge_gles_api PROPERTIES EXPORT_NAME gles_api)
  TARGET_INCLUDE_DIRECTORIES(edge_gles_api SYSTEM INTERFACE ${EDGE_GLES_INCLUDE_DIR})
  TARGET_LINK_LIBRARIES(edge_gles_api INTERFACE ${EDGE_GLES_LIBRARY})
  MESSAGE(STATUS "OpenGL ES backend: ${EDGE_GLES_LIBRARY}")
ELSE()
  UNSET(EDGE_USE_GLES_SHADERS CACHE)       # (src/CMakeLists.txt sets it; a build once configured with the backend keeps it)
ENDIF()

# The Metal backend (src/metal_*, Objective-C++): on by default on Apple platforms, which have no other renderer worth
# having (Vulkan only through MoltenVK, OpenGL ES not at all on iOS).  Apple-silicon Macs and iOS 16.3 on (std::format, above).
IF(APPLE)
  SET(EDGE_METAL_DEFAULT ON)
ELSE()
  SET(EDGE_METAL_DEFAULT OFF)
ENDIF()
OPTION(EDGE_METAL "Build the Metal renderer (Apple platforms only)." ${EDGE_METAL_DEFAULT})
IF(EDGE_METAL)
  IF(NOT APPLE)
    MESSAGE(FATAL_ERROR "EDGE_METAL: Metal is Apple's")
  ENDIF()
  ENABLE_LANGUAGE(OBJCXX)
  SET(EDGE_USE_METAL TRUE)
  ADD_LIBRARY(edge_metal_api INTERFACE)
  SET_TARGET_PROPERTIES(edge_metal_api PROPERTIES EXPORT_NAME metal_api)
  TARGET_LINK_LIBRARIES(edge_metal_api INTERFACE "-framework Metal" "-framework QuartzCore" "-framework Foundation")
  MESSAGE(STATUS "Metal backend")
ELSE()
  UNSET(EDGE_USE_METAL_SHADERS CACHE)
ENDIF()

# The asset tools: edge_asset (what makes .edge models: conversion, atlases, levels of detail, tiles, BC7 and ASTC), edge_convert,
# edge_shot and pyedge's convert_gltf_to_edge, with what they need (meshoptimizer, bc7enc_rdo, astc-encoder, xatlas).  Off, edge is
# the runtime alone: it draws and loads models (.edge, glTF) and needs none of them.
OPTION(EDGE_ASSET_TOOLS "Build edge_asset, edge_convert and edge_shot, and pyedge's conversion (meshoptimizer, the encoders)." ON)

# edge_shot, the program that draws a model once (tools/edge_shot.cc): a picture, a contact sheet or, with edge_sdl, a window.  On where edge is
# built by itself with a renderer that draws it (Vulkan or Metal) and the asset tools; a parent asks for it (a phone's build cannot run it).
IF(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND (EDGE_USE_VULKAN OR EDGE_USE_METAL) AND EDGE_ASSET_TOOLS)
  SET(EDGE_SHOT_DEFAULT ON)
ELSE()
  SET(EDGE_SHOT_DEFAULT OFF)
ENDIF()
OPTION(EDGE_SHOT "Build edge_shot: a model drawn to a PNG, a contact sheet, or a window (with EDGE_SDL)." ${EDGE_SHOT_DEFAULT})
IF(EDGE_SHOT AND NOT EDGE_ASSET_TOOLS)
  MESSAGE(FATAL_ERROR "EDGE_SHOT: edge_shot links edge_asset, which EDGE_ASSET_TOOLS=OFF leaves out")
ENDIF()

# Dependencies.  By default every one is fetched at configure (FetchContent, pinned) and built here.  EDGE_SYSTEM_DEPS takes what a
# package manager (Conan, vcpkg) or the system provides instead, with find_package, and maps its targets onto the ones edge links:
#
#   cereal         find_package(cereal CONFIG)          cereal::cereal
#   stb            find_package(stb CONFIG) (Conan)     stb::stb; else its headers, find_path(STB_INCLUDE_DIRS stb_image.h) (vcpkg,
#                                                       Debian's stb/ under the include directory)
#   meshoptimizer  find_package(meshoptimizer CONFIG)   meshoptimizer::meshoptimizer
#   Dear ImGui     find_package(imgui CONFIG)           imgui::imgui (EDGE_IMGUI, unless the parent has a target imgui)
#   nanobind       find_package(nanobind CONFIG)        pip's (python -m nanobind --cmake_dir), EDGE_PYTHON
#   rove           find_package(rove 3 CONFIG)          rove::rove (unless the parent has a target rove, or EDGE_ROVE_SOURCE_DIR)
#   SDL3, Vulkan   found either way                     SDL3::SDL3 (EDGE_SDL), Vulkan::Vulkan
#
# Fetched either way: fastgltf, which edge patches (etc/patches; ConanCenter's and vcpkg's are not), with the simdjson it pins; and
# the asset tools' encoders no package manager has (bc7enc_rdo, astc-encoder, xatlas); SPIRV-Cross, for the host's shader_cross
# (GLES, Metal: tools/shader_cross).  Boost.Test (EDGE_TESTS) is found either way.
OPTION(EDGE_SYSTEM_DEPS "Find edge's dependencies (find_package: Conan, vcpkg, the system) instead of fetching them" OFF)

INCLUDE(FetchContent)
# FetchContent_MakeAvailable without the dependency's own install rules (EXCLUDE_FROM_ALL): what edge links of it privately is installed
# with edge's package, under edge's names, where edge installs at all.  (FetchContent_Declare takes EXCLUDE_FROM_ALL from CMake 3.28.)
MACRO(EDGE_FETCH_EXCLUDED name)
  IF(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    FetchContent_Declare(${name} ${ARGN} EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(${name})
  ELSE()
    FetchContent_Declare(${name} ${ARGN})
    FetchContent_GetProperties(${name})
    IF(NOT ${name}_POPULATED)
      FetchContent_Populate(${name})
      ADD_SUBDIRECTORY(${${name}_SOURCE_DIR} ${${name}_BINARY_DIR} EXCLUDE_FROM_ALL)
    ENDIF()
  ENDIF()
ENDMACRO()

IF(EDGE_IMGUI AND NOT TARGET imgui)
  IF(EDGE_SYSTEM_DEPS)
    FIND_PACKAGE(imgui CONFIG REQUIRED)
    ADD_LIBRARY(imgui ALIAS imgui::imgui)
  ELSEIF(NOT EDGE_TOP_LEVEL)
    MESSAGE(FATAL_ERROR "EDGE_IMGUI: there is no target imgui (make the one the parent uses before adding edge)")
  ELSE()
    # the core alone (no platform backend)
    FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui GIT_TAG f1cc2ae15e53a861a874c3034aae6798fde194ab
      SOURCE_SUBDIR no_cmake_here)
    FetchContent_MakeAvailable(imgui)
    ADD_LIBRARY(imgui STATIC ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
    TARGET_INCLUDE_DIRECTORIES(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR})
    # (edge's tests' ImGui, standing for a program's: never installed)
    SET_TARGET_PROPERTIES(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
  ENDIF()
ENDIF()

# Boost.Test, for the tests only (rove finds the Boost headers it uses itself).
IF(EDGE_TESTS)
  FIND_PACKAGE(Boost 1.74 CONFIG REQUIRED COMPONENTS unit_test_framework)
ENDIF()

# fastgltf, built here with edge's patches (etc/patches): 0.9.0 drops a scene without nodes, which renumbers the scenes after it.
# Only the loader (gltf_loader.cc) sees it.  Fetched whatever EDGE_SYSTEM_DEPS says: a package manager's is not patched.
# as C++20, as edge: its headers declare other types (std::span) in C++20 than in C++17, which it builds as otherwise
SET(FASTGLTF_COMPILE_AS_CPP20 ON CACHE BOOL "" FORCE)
# with the simdjson it pins (its -I comes before every -isystem): not one found elsewhere, whose headers and library may not match
SET(CMAKE_DISABLE_FIND_PACKAGE_simdjson ON)
EDGE_FETCH_EXCLUDED(fastgltf
  GIT_REPOSITORY https://github.com/spnda/fastgltf
  GIT_TAG 0d1b67a28c4950ea2deb796702006dcbe31e02b3  # v0.9.0
  PATCH_COMMAND git reset --hard --quiet && git apply ${EDGE_ROOT}/etc/patches/fastgltf-0.9.0-scenes-without-nodes.patch
  UPDATE_DISCONNECTED TRUE)
# (installed with edge as edge::fastgltf, libedge_fastgltf: not a fastgltf of the system's, which is not patched)
SET_TARGET_PROPERTIES(fastgltf PROPERTIES POSITION_INDEPENDENT_CODE ON OUTPUT_NAME edge_fastgltf)

# cereal: the .edge file's serialization (serialized_model.h, a public header).  Header-only: fetched, not built.  edge links edge_cereal.
IF(EDGE_SYSTEM_DEPS)
  FIND_PACKAGE(cereal CONFIG REQUIRED)
  ADD_LIBRARY(edge_cereal ALIAS cereal::cereal)
ELSE()
  FetchContent_Declare(
    cereal
    GIT_REPOSITORY https://github.com/USCiLab/cereal
    GIT_TAG ebef1e929807629befafbb2918ea1a08c7194554  # v1.3.2
    SOURCE_SUBDIR no_cmakelists_here)
  FetchContent_MakeAvailable(cereal)
  ADD_LIBRARY(edge_cereal INTERFACE)
  TARGET_INCLUDE_DIRECTORIES(edge_cereal SYSTEM INTERFACE $<BUILD_INTERFACE:${cereal_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  SET_TARGET_PROPERTIES(edge_cereal PROPERTIES EXPORT_NAME cereal)
ENDIF()

# stb: image decoding and writing, DXT compression, font rasterising (stb_impl.cc compiles them).  Header-only.  edge links edge_stb.
IF(EDGE_SYSTEM_DEPS)
  FIND_PACKAGE(stb CONFIG QUIET)
  IF(TARGET stb::stb)
    ADD_LIBRARY(edge_stb ALIAS stb::stb)
  ELSE()
    FIND_PATH(STB_INCLUDE_DIRS stb_image.h PATH_SUFFIXES stb)
    IF(NOT STB_INCLUDE_DIRS)
      MESSAGE(FATAL_ERROR "EDGE_SYSTEM_DEPS: stb is not found (no package stb, and no stb_image.h: STB_INCLUDE_DIRS)")
    ENDIF()
    ADD_LIBRARY(edge_stb INTERFACE)
    TARGET_INCLUDE_DIRECTORIES(edge_stb SYSTEM INTERFACE ${STB_INCLUDE_DIRS})
  ENDIF()
ELSE()
  FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb
    GIT_TAG 5736b15f7ea0ffb08dd38af21067c314d6a3aae9
    SOURCE_SUBDIR no_cmakelists_here)
  FetchContent_MakeAvailable(stb)
  ADD_LIBRARY(edge_stb INTERFACE)
  TARGET_INCLUDE_DIRECTORIES(edge_stb SYSTEM INTERFACE ${stb_SOURCE_DIR})
ENDIF()

# rove, the math library.  The layout edge is made for has rove and edge side by side, as git submodules of a game (or checkouts):
#
#   add_subdirectory(rove)
#   add_subdirectory(edge)        # takes the parent's target rove: nothing of rove is looked for or fetched
#
# Else EDGE_ROVE_SOURCE_DIR when given; else with EDGE_SYSTEM_DEPS an installed rove (find_package(rove 3 CONFIG)); else a checkout
# beside edge (../rove), else an installed rove, else rove fetched from GitHub at EDGE_ROVE_GIT_TAG.  From its sources, edge adds it with
# its own choice of rove's options (and pyedge's build makes rove's Python module too).
set(EDGE_ROVE_SOURCE_DIR "" CACHE PATH "Path to rove source tree used by edge.")
set(EDGE_ROVE_GIT_TAG "v3.0.1" CACHE STRING "The rove version fetched when no rove source tree is found.")
if(TARGET rove)
  get_target_property(EDGE_ROVE_SOURCE_DIR rove SOURCE_DIR)
elseif(TARGET rove::rove)
  # (found before edge was added)
elseif(EDGE_SYSTEM_DEPS AND NOT EDGE_ROVE_SOURCE_DIR)
  find_package(rove 3 CONFIG REQUIRED)
elseif(NOT EDGE_ROVE_SOURCE_DIR)
  if(EXISTS "${EDGE_SOURCE_DIR}/../rove/CMakeLists.txt")
    set(EDGE_ROVE_SOURCE_DIR "${EDGE_SOURCE_DIR}/../rove")
  else()
    find_package(rove 3 CONFIG QUIET)
    if(NOT rove_FOUND)
      # (fetched only: it is added below, with edge's choice of its options)
      message(STATUS "rove: no ../rove and no installed rove 3: fetching ${EDGE_ROVE_GIT_TAG}")
      FetchContent_Declare(rove GIT_REPOSITORY https://github.com/mnvl/rove GIT_TAG ${EDGE_ROVE_GIT_TAG} GIT_SHALLOW TRUE
        SOURCE_SUBDIR no_cmakelists_here)
      FetchContent_MakeAvailable(rove)
      set(EDGE_ROVE_SOURCE_DIR "${rove_SOURCE_DIR}")
    endif()
  endif()
endif()
if(TARGET rove::rove AND NOT TARGET rove)
  message(STATUS "rove: ${rove_DIR}")
elseif(TARGET rove)
  message(STATUS "rove: the parent's target rove (${EDGE_ROVE_SOURCE_DIR})")
else()
  message(STATUS "rove: ${EDGE_ROVE_SOURCE_DIR}")
endif()
if(TARGET rove OR TARGET rove::rove)
  set(EDGE_ADDS_ROVE OFF)
  if(EDGE_INSTALL AND TARGET rove AND NOT ROVE_INSTALL)
    get_target_property(EDGE_ROVE_IMPORTED rove IMPORTED)
    if(NOT EDGE_ROVE_IMPORTED)
      message(FATAL_ERROR "EDGE_INSTALL: edge's package needs rove's, and the parent's rove has no install rules: turn ROVE_INSTALL on too")
    endif()
  endif()
else()
  set(EDGE_ADDS_ROVE ON)
  if(NOT EXISTS "${EDGE_ROVE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "EDGE_ROVE_SOURCE_DIR (${EDGE_ROVE_SOURCE_DIR}) is not a rove source tree")
  endif()
endif()
# rove's headers, which edge's pipeline hash covers (src/CMakeLists.txt): its sources', else an installed rove's include/rove
if(EDGE_ROVE_SOURCE_DIR)
  set(EDGE_ROVE_HASHED_DIR "${EDGE_ROVE_SOURCE_DIR}/src")
else()
  get_target_property(EDGE_ROVE_INCLUDES rove::rove INTERFACE_INCLUDE_DIRECTORIES)
  list(GET EDGE_ROVE_INCLUDES 0 EDGE_ROVE_INCLUDE)
  set(EDGE_ROVE_HASHED_DIR "${EDGE_ROVE_INCLUDE}/rove")
endif()

# pyedge's nanobind, the version pyrove is built with (2.4.0: the two modules share rove's types only so).  A wheel's build and
# EDGE_SYSTEM_DEPS take the one pip installed (pyproject.toml); else it is fetched, at top level so nanobind_add_module() is there in
# rove/ and src/ alike (rove's own declaration reuses this one).  rove's Python module is built beside pyedge's only where both are
# built from sources here (not a wheel, which imports pip's pyrove, and not with EDGE_SYSTEM_DEPS).
if(EDGE_PYTHON)
  find_package(Python 3.8 COMPONENTS Interpreter Development.Module REQUIRED)
  if(SKBUILD OR EDGE_SYSTEM_DEPS)
    if(NOT nanobind_ROOT AND NOT nanobind_DIR)
      execute_process(COMMAND ${Python_EXECUTABLE} -m nanobind --cmake_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_ROOT ERROR_QUIET)
    endif()
    find_package(nanobind CONFIG REQUIRED)
    set(EDGE_ROVE_PYTHON OFF)
  else()
    FetchContent_Declare(
        nanobind
        GIT_REPOSITORY https://github.com/wjakob/nanobind
        GIT_TAG 0f9ce749b257fdfe701edb3cf6f7027ba029434a  # v2.4.0
    )
    FetchContent_MakeAvailable(nanobind)
    set(EDGE_ROVE_PYTHON ON)
  endif()
else()
  set(EDGE_ROVE_PYTHON OFF)
endif()

if(EDGE_ADDS_ROVE)
  set(ROVE_TESTS OFF CACHE BOOL "" FORCE)
  set(ROVE_PYTHON ${EDGE_ROVE_PYTHON} CACHE BOOL "" FORCE)
  # (edge's package needs rove's beside it: find_dependency(rove))
  set(ROVE_INSTALL ${EDGE_INSTALL} CACHE BOOL "" FORCE)
endif()

# What only the asset tools need (EDGE_ASSET_TOOLS).
if(EDGE_ASSET_TOOLS)
  # meshoptimizer: the edge-collapse simplifier that makes a model's levels of detail (src/mesh_lod.h).  edge_asset links
  # meshoptimizer::meshoptimizer.
  if(EDGE_SYSTEM_DEPS)
    # (its package takes no version but its own: meshopt_simplifyWithAttributes takes vertex_lock from 0.21; edge pins 0.22)
    find_package(meshoptimizer CONFIG REQUIRED)
    if(meshoptimizer_VERSION AND meshoptimizer_VERSION VERSION_LESS 0.21)
      message(FATAL_ERROR "EDGE_SYSTEM_DEPS: meshoptimizer ${meshoptimizer_VERSION} (${meshoptimizer_DIR}) is too old: 0.21 or newer is needed")
    endif()
  else()
    set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
    set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
    set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    edge_fetch_excluded(meshoptimizer
        GIT_REPOSITORY https://github.com/zeux/meshoptimizer
        GIT_TAG 4affad044571506a5724c9a6f15424f43e86f731  # v0.22
    )
    # (installed with edge as edge::meshoptimizer, libedge_meshoptimizer: edge_asset links it privately)
    set_target_properties(meshoptimizer PROPERTIES POSITION_INDEPENDENT_CODE ON OUTPUT_NAME edge_meshoptimizer)
    if(NOT TARGET meshoptimizer::meshoptimizer)
      add_library(meshoptimizer::meshoptimizer ALIAS meshoptimizer)
    endif()
  endif()

  # bc7enc: the BC7 encoder of the textures edge_convert makes (src/image_compress.h), and its decoder, which the tests check the
  # encoder against.  The repository is fetched for two files each, not built by its own CMakeLists (SOURCE_SUBDIR names none).
  FetchContent_Declare(
      bc7enc_rdo
      GIT_REPOSITORY https://github.com/richgel999/bc7enc_rdo
      GIT_TAG b9438627eef73a1157e84201b6fa6eb2ffd6d9f0
      SOURCE_SUBDIR no_cmakelists_here
  )
  FetchContent_MakeAvailable(bc7enc_rdo)
  add_library(edge_bc7enc STATIC
      ${bc7enc_rdo_SOURCE_DIR}/bc7enc.cpp
      ${bc7enc_rdo_SOURCE_DIR}/bc7decomp.cpp)
  target_include_directories(edge_bc7enc SYSTEM PUBLIC $<BUILD_INTERFACE:${bc7enc_rdo_SOURCE_DIR}>)
  set_target_properties(edge_bc7enc PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME bc7enc)

  # astc-encoder (ARM's, Apache-2.0): the ASTC encoder of the textures edge_convert --astc makes (src/image_compress.h), and the decoder the
  # tests check it with.  Its core library's sources built here, not by its own CMakeLists (which builds per instruction set and a CLI);
  # it picks its SIMD from the compiler's target (NEON on arm64, SSE2 on x86-64).  Invariant floating point: the same bytes on every build.
  FetchContent_Declare(
      astcenc
      GIT_REPOSITORY https://github.com/ARM-software/astc-encoder
      GIT_TAG 30aabb3f42406df45a910d8496f9bee17eeba9bb  # 5.3.0
      SOURCE_SUBDIR no_cmakelists_here
  )
  FetchContent_MakeAvailable(astcenc)
  file(GLOB EDGE_ASTCENC_SOURCES ${astcenc_SOURCE_DIR}/Source/astcenc_*.cpp)
  add_library(edge_astcenc STATIC ${EDGE_ASTCENC_SOURCES})
  target_include_directories(edge_astcenc SYSTEM PUBLIC $<BUILD_INTERFACE:${astcenc_SOURCE_DIR}/Source>)
  target_compile_options(edge_astcenc PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-ffp-contract=off>)
  set_target_properties(edge_astcenc PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME astcenc)

  # xatlas: the unwrapping of a model's meshes into one set of texture coordinates (src/model_atlas.h).  Fetched for its two files, as bc7enc is.
  FetchContent_Declare(
      xatlas
      GIT_REPOSITORY https://github.com/jpcy/xatlas
      GIT_TAG f700c7790aaa030e794b52ba7791a05c085faf0c
      SOURCE_SUBDIR no_cmakelists_here
  )
  FetchContent_MakeAvailable(xatlas)
  add_library(edge_xatlas STATIC ${xatlas_SOURCE_DIR}/source/xatlas/xatlas.cpp)
  target_include_directories(edge_xatlas SYSTEM PUBLIC $<BUILD_INTERFACE:${xatlas_SOURCE_DIR}/source/xatlas>)
  set_target_properties(edge_xatlas PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME xatlas)
endif()

if(EDGE_ADDS_ROVE)
  add_subdirectory("${EDGE_ROVE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/rove-build")
endif()

INCLUDE(EdgeShaders)
INCLUDE(EdgeRuntime)
INCLUDE(EdgeModels)
# the version of the .edge files this edge writes and reads (src/serialized_model.h), for a build that keeps models it made
FILE(STRINGS ${EDGE_ROOT}/src/serialized_model.h EDGE_MODEL_BINARY_VERSION REGEX "MODEL_BINARY_VERSION = [0-9]+U")
STRING(REGEX REPLACE ".*MODEL_BINARY_VERSION = ([0-9]+)U.*" "\\1" EDGE_MODEL_BINARY_VERSION "${EDGE_MODEL_BINARY_VERSION}")
IF(NOT EDGE_MODEL_BINARY_VERSION MATCHES "^[0-9]+$")
  MESSAGE(FATAL_ERROR "no MODEL_BINARY_VERSION in src/serialized_model.h")
ENDIF()
SET(EDGE_MODEL_BINARY_VERSION ${EDGE_MODEL_BINARY_VERSION} CACHE INTERNAL "the .edge format version")
SET_PROPERTY(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${EDGE_ROOT}/src/serialized_model.h)
# the light of edge_shot and of Python model viewers, beside pyedge's module (lib/view_shaders); both find it with edge::view_shader_dir()
IF(EDGE_USE_VULKAN OR EDGE_USE_METAL)
  SET(EDGE_VIEW_SHADER_BUILD_DIR ${LIBRARY_OUTPUT_PATH}/view_shaders)
ENDIF()
ADD_SUBDIRECTORY(src)
IF(SKBUILD AND NOT (EDGE_USE_VULKAN OR EDGE_USE_METAL))
  MESSAGE(FATAL_ERROR "a wheel of pyedge draws with Vulkan (or Metal): the Vulkan headers, loader and glslangValidator are needed")
ENDIF()
IF(EDGE_VIEW_SHADER_BUILD_DIR)
  EDGE_ADD_SHADERS(edge_view_shaders DESTINATION ${EDGE_VIEW_SHADER_BUILD_DIR} SOURCES ${EDGE_ROOT}/tools/shaders/lit.frag)
  # (a wheel's: view_shaders beside the module)
  IF(SKBUILD AND EDGE_PYTHON)
    INSTALL(FILES ${edge_view_shaders_OUTPUTS} DESTINATION pyedge/view_shaders)
  ENDIF()
  IF(EDGE_PYTHON)
    ADD_DEPENDENCIES(pyedge edge_view_shaders)
  ENDIF()
  IF(EDGE_SHOT)
    ADD_DEPENDENCIES(edge_shot edge_view_shaders)
  ENDIF()
ENDIF()

# Installed (EDGE_INSTALL): lib/ the libraries, include/edge/ their headers (the real ones: <edge/x.h> is the header itself, and only
# include/ is on the path), share/edge/ the shaders (src/CMakeLists.txt) and view shaders, bin/ the tools, lib/cmake/edge/ the package
# and the helpers.  The libraries edge builds of its dependencies and links privately are in edge's package under edge's names
# (edge::fastgltf, libedge_fastgltf.a, ...): never taken for a system's own.  Bundled, what the public headers include is installed as
# itself: cereal's headers; rove's own rules install rove where edge added it (ROVE_INSTALL).  Nothing of ImGui: edge::imgui is a
# header, compiled against the program's.
IF(EDGE_INSTALL)
  INCLUDE(CMakePackageConfigHelpers)
  SET(EDGE_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/edge)
  SET(EDGE_INSTALL_GLSLDIR ${CMAKE_INSTALL_DATADIR}/edge/glsl)
  SET(EDGE_INSTALL_SHADERDIR ${CMAKE_INSTALL_DATADIR}/edge/shaders)
  SET(EDGE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})

  # (what this build made: not a found package's target, nor an alias of one)
  SET(EDGE_EXPORTED)
  FOREACH(target edge fastgltf edge_cereal edge_asset meshoptimizer edge_bc7enc edge_astcenc edge_xatlas edge_convert edge_shot edge_sdl
      edge_imgui edge_gles_api edge_metal_api)
    IF(TARGET ${target})
      GET_TARGET_PROPERTY(imported ${target} IMPORTED)
      GET_TARGET_PROPERTY(aliased ${target} ALIASED_TARGET)
      IF(NOT imported AND NOT aliased)
        LIST(APPEND EDGE_EXPORTED ${target})
      ENDIF()
    ENDIF()
  ENDFOREACH()
  IF(NOT TARGET edge_asset)
    LIST(REMOVE_ITEM EDGE_EXPORTED meshoptimizer)
  ENDIF()
  IF(cereal_SOURCE_DIR AND NOT EDGE_SYSTEM_DEPS)
    INSTALL(DIRECTORY ${cereal_SOURCE_DIR}/include/cereal DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  ENDIF()
  INSTALL(TARGETS ${EDGE_EXPORTED} EXPORT edgeTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  INSTALL(EXPORT edgeTargets NAMESPACE edge:: DESTINATION ${EDGE_INSTALL_CMAKEDIR})

  # The public headers, those include/edge has a shim of, and the headers of src/ they include (#include "x.h"), but those of what
  # this build has not (a backend, edge_sdl, edge_imgui, edge_asset).
  FILE(GLOB EDGE_HEADERS RELATIVE ${EDGE_ROOT}/include/edge ${EDGE_ROOT}/include/edge/*.h)
  SET(EDGE_HEADERS_TO_SCAN ${EDGE_HEADERS})
  WHILE(EDGE_HEADERS_TO_SCAN)
    LIST(POP_FRONT EDGE_HEADERS_TO_SCAN header)
    FILE(STRINGS ${EDGE_ROOT}/src/${header} includes REGEX "^#include \"[^\"/]+\"")
    FOREACH(line ${includes})
      STRING(REGEX REPLACE "^#include \"([^\"]+)\".*" "\\1" included "${line}")
      IF(EXISTS ${EDGE_ROOT}/src/${included} AND NOT included IN_LIST EDGE_HEADERS)
        LIST(APPEND EDGE_HEADERS ${included})
        LIST(APPEND EDGE_HEADERS_TO_SCAN ${included})
      ENDIF()
    ENDFOREACH()
  ENDWHILE()
  IF(NOT EDGE_USE_VULKAN)
    LIST(FILTER EDGE_HEADERS EXCLUDE REGEX "^(vulkan_.*|frame_presenter|bundled_runtime)\\.h$")
  ENDIF()
  IF(NOT EDGE_USE_GLES)
    LIST(FILTER EDGE_HEADERS EXCLUDE REGEX "^gles_")
  ENDIF()
  IF(NOT EDGE_USE_METAL)
    LIST(FILTER EDGE_HEADERS EXCLUDE REGEX "^metal_")
  ENDIF()
  IF(NOT TARGET edge_sdl)
    LIST(FILTER EDGE_HEADERS EXCLUDE REGEX "_sdl_")
  ENDIF()
  IF(NOT TARGET edge_imgui)
    LIST(FILTER EDGE_HEADERS EXCLUDE REGEX "^imgui_")
  ENDIF()
  IF(NOT TARGET edge_asset)
    LIST(FILTER EDGE_HEADERS EXCLUDE REGEX "^(image_compress|image_mips|mesh_lod|mesh_tiles|model_atlas.*|model_convert|model_textures)\\.h$")
  ENDIF()
  LIST(TRANSFORM EDGE_HEADERS PREPEND ${EDGE_ROOT}/src/)
  INSTALL(FILES ${EDGE_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/edge)

  IF(EDGE_VIEW_SHADER_BUILD_DIR)
    INSTALL(FILES ${edge_view_shaders_OUTPUTS} DESTINATION ${CMAKE_INSTALL_DATADIR}/edge/view_shaders)
  ENDIF()
  INSTALL(FILES ${EDGE_CMAKE_DIR}/EdgeShaders.cmake ${EDGE_CMAKE_DIR}/EdgeGles.cmake ${EDGE_CMAKE_DIR}/EdgeMetal.cmake
    ${EDGE_CMAKE_DIR}/EdgeRuntime.cmake ${EDGE_CMAKE_DIR}/EdgeModels.cmake ${EDGE_CMAKE_DIR}/edge_convert_model.cmake
    DESTINATION ${EDGE_INSTALL_CMAKEDIR})
  INSTALL(FILES ${EDGE_ROOT}/LICENSE.md ${EDGE_ROOT}/THIRD_PARTY.md ${EDGE_ROOT}/THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/edge)

  # the package: what it was built with, for edgeConfig.cmake.in
  FOREACH(flag EDGE_USE_VULKAN EDGE_USE_GLES EDGE_USE_METAL EDGE_SYSTEM_DEPS)
    IF(${flag})
      SET(${flag}_BOOL ON)
    ELSE()
      SET(${flag}_BOOL OFF)
    ENDIF()
  ENDFOREACH()
  FOREACH(part sdl imgui asset)
    IF(TARGET edge_${part})
      STRING(TOUPPER ${part} PART)
      SET(EDGE_INSTALLED_${PART} ON)
    ELSE()
      STRING(TOUPPER ${part} PART)
      SET(EDGE_INSTALLED_${PART} OFF)
    ENDIF()
  ENDFOREACH()
  SET(EDGE_RUNTIME_SHADER_NAMES)
  FOREACH(shader ${EDGE_RUNTIME_SHADERS})
    GET_FILENAME_COMPONENT(name ${shader} NAME)
    LIST(APPEND EDGE_RUNTIME_SHADER_NAMES ${name})
  ENDFOREACH()
  CONFIGURE_PACKAGE_CONFIG_FILE(${EDGE_CMAKE_DIR}/edgeConfig.cmake.in ${PROJECT_BINARY_DIR}/edgeConfig.cmake
    INSTALL_DESTINATION ${EDGE_INSTALL_CMAKEDIR}
    PATH_VARS EDGE_INSTALL_GLSLDIR EDGE_INSTALL_SHADERDIR EDGE_INSTALL_BINDIR)
  WRITE_BASIC_PACKAGE_VERSION_FILE(${PROJECT_BINARY_DIR}/edgeConfigVersion.cmake VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
  INSTALL(FILES ${PROJECT_BINARY_DIR}/edgeConfig.cmake ${PROJECT_BINARY_DIR}/edgeConfigVersion.cmake DESTINATION ${EDGE_INSTALL_CMAKEDIR})
ENDIF()

IF(EDGE_TESTS)
  ADD_SUBDIRECTORY(test)
ENDIF()
