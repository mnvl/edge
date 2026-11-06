# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

cmake_minimum_required(VERSION 3.20)
project(edge_example CXX)
set(CMAKE_CXX_STANDARD 20)

# (a parent that adds rove and edge as subdirectories, as the CI does, has the targets already)
if(NOT TARGET edge::edge)
  find_package(edge CONFIG REQUIRED)
endif()

add_executable(edge_example example.cc)
target_link_libraries(edge_example PRIVATE edge::edge)

# (with Vulkan) a game's shader, compiled with glslangValidator against edge.glsl, and edge's own shaders copied beside the program,
# where edge looks for them
if(EDGE_RUNTIME_SHADERS)
  edge_add_shaders(edge_example_shaders DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/shaders SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tint.frag)
  set_target_properties(edge_example PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_CURRENT_BINARY_DIR}>)
  edge_bundle_runtime(edge_example_runtime DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  add_dependencies(edge_example edge_example_shaders edge_example_runtime)
endif()
