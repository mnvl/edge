# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# shader_cross: SPIR-V to GLSL ES for edge's OpenGL ES renderer, and to MSL for the Metal one (shader_cross.cc).  A project of its own so that a cross build (the
# phone's) makes it for the machine that builds: the build runs it.
cmake_minimum_required(VERSION 3.15)
project(SHADER_CROSS CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
include(FetchContent)
FetchContent_Declare(spirv_cross
  GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross
  GIT_TAG vulkan-sdk-1.4.350.1
  GIT_SHALLOW TRUE)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spirv_cross)
add_executable(shader_cross shader_cross.cc)
target_link_libraries(shader_cross PRIVATE spirv-cross-glsl spirv-cross-msl)
