# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# test_edge, edge's tests (Boost.Test, one ctest test a top-level suite), the shaders they draw with, and pyedge's tests.  Their assets
# are in data/ (and the scripts that make them), their shaders in shaders/; a test finds both through test_fixture.h.

SET(EDGE_TEST_SOURCES
  test_animated_mesh.cc
  test_animated_mesh_cache.cc
  test_animated_mesh_culling.cc
  test_animated_model.cc
  test_backends.h
  test_blend_order.cc
  test_bundled_runtime.cc
  test_camera.cc
  test_custom_attributes.cc
  test_debug_lines.cc
  test_dds.cc
  test_depth_texture.cc
  test_draw_call.cc
  test_draw_checks.cc
  test_dynamic_mesh.cc
  test_edge_file.cc
  test_untrusted_files.cc
  test_fixture.h
  test_font.cc
  test_frame_resources.cc
  test_fullscreen_copy.cc
  test_gltf_loader.cc
  test_gltf_pbr.cc
  test_gpu_timer.cc
  test_image.cc
  test_image_compress.cc
  test_image_mips.cc
  test_instancing.cc
  test_load_model.cc
  test_lod_choice.cc
  test_log.cc
  test_main.cc
  test_material.cc
  test_material_parameters.cc
  test_mesh.cc
  test_mesh_builder.cc
  test_mesh_lod.cc
  test_mesh_tiles.cc
  test_model.cc
  test_model_builder.cc
  test_model_cache.cc
  test_model_atlas.cc
  test_model_convert.cc
  test_model_textures.cc
  test_model_lod.cc
  test_motion.cc
  test_occlusion.cc
  test_pose_bounds.cc
  test_renderer.cc
  test_renderer_contract.cc
  test_render_targets.cc
  test_sampler.cc
  test_scheduler.cc
  test_shader_dirs.cc
  test_shader_includes.cc
  test_shader_interface.cc
  test_skin_palette.cc
  test_static_mesh.cc
  test_stencil_texture.cc
  test_texture.cc
  test_texture_array.cc
  test_texture_blending.cc
  test_texture_io.cc
  test_threads.cc
  test_uber_draw_call.cc
  test_vertex.cc
  test_vertex_color_shader.cc
  test_vertex_colors.cc
  test_vertex_texture.cc
  test_vulkan_blend.cc
  test_vulkan_buffer_memory.cc
  test_vulkan_cull.cc
  test_vulkan_descriptor_sets.cc
  test_vulkan_fixture.h
  test_vulkan_frames_in_flight.cc
  test_vulkan_gpu_timer.cc
  test_vulkan_instance.cc
  test_vulkan_pipeline_cache.cc
  test_vulkan_ownership.cc
  test_vulkan_present.cc
  test_vulkan_render_pass_cache.cc
  test_vulkan_renderer.cc
  test_vulkan_uploader.cc
  test_weights_sampler.cc
)

SET(EDGE_GLES_TEST_SOURCES
  test_gles_context.cc
  test_gles_fixture.h
  test_gles_renderer.cc
)

SET(EDGE_METAL_TEST_SOURCES
  test_metal_fixture.h
  test_metal_renderer.cc
)

IF(NOT EDGE_USE_VULKAN)
  SET(VULKAN_TEST_SOURCES
    test_bundled_runtime.cc
    test_renderer.cc
    test_texture_blending.cc
    test_font.cc
    test_shader_includes.cc
    test_vulkan_renderer.cc
    test_vulkan_present.cc
    test_vulkan_ownership.cc
    test_threads.cc
    test_vulkan_gpu_timer.cc
    test_vulkan_instance.cc
    test_vulkan_uploader.cc
    test_vulkan_frames_in_flight.cc
    test_vulkan_render_pass_cache.cc
    test_vulkan_buffer_memory.cc
    test_vulkan_descriptor_sets.cc
    test_vulkan_pipeline_cache.cc
    test_vertex_texture.cc
    test_frame_resources.cc
    test_fullscreen_copy.cc
    test_dynamic_mesh.cc
    test_vertex_color_shader.cc
    test_vertex_colors.cc
    test_gltf_pbr.cc
    test_dds.cc
    test_texture_array.cc
    test_instancing.cc
    test_material_parameters.cc
    test_blend_order.cc
    test_debug_lines.cc
    test_stencil_texture.cc
    test_vulkan_blend.cc
    test_vulkan_cull.cc
  )
  LIST(REMOVE_ITEM EDGE_TEST_SOURCES ${VULKAN_TEST_SOURCES})
ENDIF()

# The tests' own shaders, beside nothing edge ships: test_fixture.h's test_shader() names them by their path (and the GLES renderer
# finds their translations beside them).
IF(EDGE_USE_VULKAN OR EDGE_USE_GLES OR EDGE_USE_METAL)
  SET(EDGE_TEST_SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
  FILE(GLOB EDGE_TEST_SHADER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag)
  LIST(FILTER EDGE_TEST_SHADER_SOURCES EXCLUDE REGEX "\\.es\\.(vert|frag)$")    # (GLSL ES written by hand: test_gles_renderer.cc reads them)
  EDGE_ADD_SHADERS(edge_test_shaders DESTINATION ${EDGE_TEST_SHADER_DIR} SOURCES ${EDGE_TEST_SHADER_SOURCES})
ENDIF()

IF(EDGE_USE_GLES AND EDGE_SDL)
  LIST(APPEND EDGE_TEST_SOURCES ${EDGE_GLES_TEST_SOURCES})
ENDIF()
IF(EDGE_USE_METAL)
  LIST(APPEND EDGE_TEST_SOURCES ${EDGE_METAL_TEST_SOURCES})
  IF(EDGE_SDL)
    LIST(APPEND EDGE_TEST_SOURCES test_metal_sdl_surface.cc)
  ENDIF()
ENDIF()
IF(EDGE_USE_VULKAN AND EDGE_SDL)
  LIST(APPEND EDGE_TEST_SOURCES test_vulkan_sdl_surface.cc)
ENDIF()
IF(EDGE_USE_VULKAN AND EDGE_IMGUI)
  LIST(APPEND EDGE_TEST_SOURCES test_imgui_renderer.cc)
ENDIF()

# what tests the asset tools (edge_asset, and the encoders' decoders that check them): left out with them (EDGE_ASSET_TOOLS=OFF), and
# the cases of other suites that convert a model with EDGE_TEST_ASSET_TOOLS
IF(NOT EDGE_ASSET_TOOLS)
  LIST(REMOVE_ITEM EDGE_TEST_SOURCES test_image_compress.cc test_image_mips.cc test_load_model.cc test_mesh_lod.cc test_mesh_tiles.cc
    test_model_atlas.cc test_model_convert.cc test_model_lod.cc test_model_textures.cc)
ENDIF()

ADD_EXECUTABLE(test_edge ${EDGE_TEST_SOURCES})
TARGET_LINK_LIBRARIES(test_edge edge edge_stb Boost::unit_test_framework)
IF(EDGE_ASSET_TOOLS)
  TARGET_LINK_LIBRARIES(test_edge edge_asset edge_bc7enc edge_astcenc)
  TARGET_COMPILE_DEFINITIONS(test_edge PRIVATE EDGE_TEST_ASSET_TOOLS)
ENDIF()
TARGET_COMPILE_DEFINITIONS(test_edge PRIVATE EDGE_ROOT="${EDGE_ROOT}")
IF(EDGE_USE_VULKAN OR EDGE_USE_GLES OR EDGE_USE_METAL)
  ADD_DEPENDENCIES(test_edge edge_test_shaders)
  TARGET_COMPILE_DEFINITIONS(test_edge PRIVATE EDGE_TEST_SHADER_DIR="${EDGE_TEST_SHADER_DIR}")
ENDIF()
IF(TARGET edge_view_shaders)
  ADD_DEPENDENCIES(test_edge edge_view_shaders)     # (test_shader_dirs.cc finds them)
ENDIF()
IF(EDGE_USE_VULKAN)
  # a bundle as a game makes one (test_bundled_runtime.cc, test_cmake_helpers.py)
  SET(EDGE_TEST_BUNDLE_DIR ${CMAKE_CURRENT_BINARY_DIR}/bundle)
  EDGE_BUNDLE_RUNTIME(edge_test_bundle DESTINATION ${EDGE_TEST_BUNDLE_DIR})
  ADD_DEPENDENCIES(test_edge edge_test_bundle)
  TARGET_COMPILE_DEFINITIONS(test_edge PRIVATE EDGE_TEST_BUNDLE_DIR="${EDGE_TEST_BUNDLE_DIR}")
ENDIF()
IF(EDGE_IMGUI)
  TARGET_LINK_LIBRARIES(test_edge edge_imgui)
ENDIF()
IF(EDGE_SDL)
  TARGET_LINK_LIBRARIES(test_edge edge_sdl)
ENDIF()
IF(EDGE_USE_GLES)
  TARGET_COMPILE_DEFINITIONS(test_edge PRIVATE EDGE_GLES_SHADER_DIR="${EDGE_GLES_SPIRV_DIR}")
ENDIF()
IF(EDGE_USE_METAL)
  TARGET_COMPILE_DEFINITIONS(test_edge PRIVATE EDGE_METAL_SHADER_DIR="${EDGE_METAL_SPIRV_DIR}")
ENDIF()
# One ctest test per top-level suite, found by listing the binary after each build (`ctest -j`).  Not the suites that time
# something: they fail under load or on a software renderer's clock, and the target edge_performance_tests runs them.
# (dds_speed is test_dds.cc's, which a build without Vulkan leaves out)
SET(EDGE_PERFORMANCE_SUITES gpu_timer_speed)
IF(EDGE_USE_VULKAN)
  LIST(APPEND EDGE_PERFORMANCE_SUITES dds_speed)
ENDIF()
LIST(JOIN EDGE_PERFORMANCE_SUITES "," EDGE_PERFORMANCE_FILTER)
ADD_CUSTOM_TARGET(edge_performance_tests
  COMMAND test_edge --run_test=${EDGE_PERFORMANCE_FILTER} --log_level=message
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS test_edge USES_TERMINAL)
SET(EDGE_SERIAL_SUITES "")
SET(EDGE_DISCOVERED_TESTS ${CMAKE_CURRENT_BINARY_DIR}/test_edge_discovered.cmake)
ADD_CUSTOM_COMMAND(TARGET test_edge POST_BUILD
  COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:test_edge> -DWORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
          -DOUT=${EDGE_DISCOVERED_TESTS} "-DSKIP=${EDGE_PERFORMANCE_SUITES}" "-DSERIAL=${EDGE_SERIAL_SUITES}"
          -P ${EDGE_CMAKE_DIR}/DiscoverBoostTests.cmake
  BYPRODUCTS ${EDGE_DISCOVERED_TESTS}
  VERBATIM)
SET_PROPERTY(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES ${EDGE_DISCOVERED_TESTS})

# pyedge's own tests (the examples among them)
IF(EDGE_PYTHON)
  ADD_TEST(NAME pyedge_py
    COMMAND ${Python_EXECUTABLE} -m pytest -q -p no:cacheprovider test -k pyedge --ignore=test/test_cmake_helpers.py --ignore=test/test_edge_shot.py --ignore=test/test_shader_cross_msl.py
    WORKING_DIRECTORY ${EDGE_ROOT})
  # (pyrove from rove's sources and the module built beside pyedge's; where rove's module is not built here, the environment's pyrove)
  IF(TARGET pyrove_bind)
    SET(EDGE_TEST_PYTHONPATH "${EDGE_ROVE_SOURCE_DIR}:${EDGE_ROOT}")
  ELSE()
    SET(EDGE_TEST_PYTHONPATH "${EDGE_ROOT}")
  ENDIF()
  SET_TESTS_PROPERTIES(pyedge_py PROPERTIES ENVIRONMENT
    "PYTHONPATH=${EDGE_TEST_PYTHONPATH};EDGE_PYTHON_BINDINGS_DIR=$<TARGET_FILE_DIR:pyedge>;PYTHONDONTWRITEBYTECODE=1;EDGE_TEST_SHADER_DIR=${EDGE_TEST_SHADER_DIR}")
  # edge_shot, judged as a program (the PNG it writes); EDGE_SHOT_GUI says whether it has --gui
  IF(EDGE_SHOT)
    ADD_TEST(NAME edge_shot_py
      COMMAND ${Python_EXECUTABLE} -m pytest -q -p no:cacheprovider test/test_edge_shot.py
      WORKING_DIRECTORY ${EDGE_ROOT})
    IF(EDGE_SDL)
      SET(EDGE_SHOT_HAS_GUI 1)
    ELSE()
      SET(EDGE_SHOT_HAS_GUI 0)
    ENDIF()
    SET_TESTS_PROPERTIES(edge_shot_py PROPERTIES ENVIRONMENT
      "EDGE_SHOT=$<TARGET_FILE:edge_shot>;EDGE_CONVERT=$<TARGET_FILE:edge_convert>;EDGE_ROOT=${EDGE_ROOT};EDGE_SHOT_GUI=${EDGE_SHOT_HAS_GUI};PYTHONDONTWRITEBYTECODE=1")
  ENDIF()
  # the CMake helpers a game builds with (EdgeModels.cmake, EdgeRuntime.cmake), each conversion test a project of its own built with Ninja
  IF(CMAKE_GENERATOR MATCHES "Ninja")
    SET(EDGE_TEST_NINJA ${CMAKE_MAKE_PROGRAM})
  ELSE()
    FIND_PROGRAM(EDGE_TEST_NINJA ninja REQUIRED)
  ENDIF()
  ADD_TEST(NAME cmake_helpers_py
    COMMAND ${Python_EXECUTABLE} -m pytest -q -p no:cacheprovider test/test_cmake_helpers.py
    WORKING_DIRECTORY ${EDGE_ROOT})
  # shader_cross --msl: edge's shaders to Metal for macOS, iphoneos and the Simulator; fails where Xcode's metal is not, it is not skipped
  IF(APPLE AND NOT IOS AND EDGE_SPIRV_DIR)
    ADD_TEST(NAME shader_cross_msl_py
      COMMAND ${Python_EXECUTABLE} -m pytest -q -p no:cacheprovider test/test_shader_cross_msl.py
      WORKING_DIRECTORY ${EDGE_ROOT})
    SET_TESTS_PROPERTIES(shader_cross_msl_py PROPERTIES ENVIRONMENT
      "SHADER_CROSS=${EDGE_SHADER_CROSS};EDGE_SPIRV_DIR=${EDGE_SPIRV_DIR};GLSLANG_VALIDATOR=${GLSLANG_VALIDATOR};PYTHONDONTWRITEBYTECODE=1")
  ENDIF()
  # (none without the asset tools: the conversion tests are skipped)
  IF(TARGET edge_convert)
    SET(EDGE_TEST_CONVERT $<TARGET_FILE:edge_convert>)
  ELSE()
    SET(EDGE_TEST_CONVERT "")
  ENDIF()
  SET_TESTS_PROPERTIES(cmake_helpers_py PROPERTIES ENVIRONMENT
    "EDGE_ROOT=${EDGE_ROOT};EDGE_CONVERT=${EDGE_TEST_CONVERT};CMAKE_COMMAND=${CMAKE_COMMAND};EDGE_NINJA=${EDGE_TEST_NINJA};EDGE_SPIRV_DIR=${EDGE_SPIRV_DIR};EDGE_TEST_BUNDLE=${EDGE_TEST_BUNDLE_DIR};EDGE_MODEL_BINARY_VERSION=${EDGE_MODEL_BINARY_VERSION};EDGE_PIPELINE_SOURCES=${PROJECT_BINARY_DIR}/src/edge_pipeline_sources.txt;PYTHONDONTWRITEBYTECODE=1")
ENDIF()
