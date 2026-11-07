# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# The sources are listed, not globbed: a new file is added here.
SET(EDGE_CORE_SOURCES
  animated_mesh.cc
  animated_mesh.h
  animated_mesh_cache.cc
  animated_mesh_cache.h
  animated_model.cc
  animated_model.h
  animation.cc
  animation.h
  buffer_usage.h
  camera.cc
  camera.h
  draw_call.cc
  draw_call.h
  font.cc
  font.h
  forward.h
  generic_vertex.h
  debug_lines.cc
  debug_lines.h
  dynamic_mesh.cc
  dynamic_mesh.h
  fullscreen_copy.cc
  fullscreen_copy.h
  gltf_animation.cc
  gltf_loader.cc
  gltf_loader.h
  gltf_materials.cc
  gltf_meshes.cc
  gltf_reader.cc
  gltf_reader.h
  gpu_timer.cc
  gpu_timer.h
  image.cc
  image.h
  index.h
  index_array.cc
  index_array.h
  index_buffer.cc
  index_buffer.h
  joint.h
  load_model.cc
  load_model.h
  lod_choice.cc
  lod_choice.h
  log.cc
  log.h
  mapping.cc
  mapping.h
  material.cc
  material.h
  mesh.cc
  mesh.h
  mesh_builder.cc
  mesh_builder.h
  model.cc
  model.h
  model_builder.cc
  model_builder.h
  model_cache.cc
  model_cache.h
  motion.cc
  motion.h
  null_index_buffer.cc
  null_index_buffer.h
  null_render_target.cc
  null_render_target.h
  null_renderer.cc
  null_renderer.h
  null_texture.cc
  null_texture.h
  null_vertex_buffer.cc
  null_vertex_buffer.h
  pipeline.h
  pixel.cc
  pixel.h
  python_bindings.h
  render_target.cc
  render_target.h
  renderer.cc
  renderer.h
  sampler.cc
  sampler.h
  scheduler.cc
  scheduler.h
  serialized_model.cc
  serialized_model.h
  shader_dirs.cc
  shader_dirs.h
  shader_program.cc
  shader_interface.cc
  shader_interface.h
  shader_program.h
  static_mesh.cc
  static_mesh.h
  static_model.cc
  static_model.h
  stb_impl.cc
  tangent_space.cc
  tangent_space.h
  texture.cc
  texture.h
  texture_io.cc
  texture_io.h
  tolerance.h
  triangle_iterator.h
  uber_draw_call.cc
  uber_draw_call.h
  vertex.h
  vertex_array.cc
  vertex_array.h
  vertex_buffer.cc
  vertex_buffer.h
  vertex_traits.cc
  vertex_traits.h
  weights_sampler.cc
  weights_sampler.h
)

# pyedge's, apart from edge (below)
SET(EDGE_PYTHON_BINDINGS_SOURCE
  bind_animation.cc
  bind_camera.cc
  bind_draw_call.cc
  bind_log.cc
  bind_lod.cc
  bind_material.cc
  bind_mesh_builder.cc
  bind_model.cc
  bind_renderer.cc
  bind_shader_program.cc
  bind_texture.cc
  python_bindings.cc
)

# edge_asset: what makes a game's models (edge_convert, pyedge's convert): glTF to .edge, atlases, levels of detail, tiles, mips, BC7 and
# ASTC.  A game that only draws them links edge alone, without xatlas, meshoptimizer and the encoders; EDGE_ASSET_TOOLS=OFF builds none
# of it.  (Its sources are hashed into the pipeline either way: pipeline.h.)
SET(EDGE_ASSET_SOURCES
  image_compress.cc
  image_compress.h
  image_mips.cc
  image_mips.h
  mesh_lod.cc
  mesh_lod.h
  mesh_tiles.cc
  mesh_tiles.h
  model_atlas.cc
  model_atlas.h
  model_atlas_common.cc
  model_atlas_common.h
  model_atlas_xatlas.cc
  model_atlas_xatlas.h
  model_convert.cc
  model_convert.h
  model_textures.cc
  model_textures.h
)

SET(EDGE_VULKAN_SOURCES
  bundled_runtime.cc
  frame_presenter.cc
  frame_presenter.h
  vulkan_commands.h
  vulkan_descriptor_cache.cc
  vulkan_descriptor_cache.h
  vulkan_device.cc
  vulkan_device.h
  vulkan_frame_ring.cc
  vulkan_frame_ring.h
  vulkan_device_context.h
  vulkan_device_info.cc
  vulkan_device_info.h
  vulkan_dynamic_uniform_buffer.h
  vulkan_error.cc
  vulkan_error.h
  vulkan_gpu_timer.cc
  vulkan_gpu_timer.h
  vulkan_image_layout.cc
  vulkan_image_layout.h
  vulkan_instance.cc
  vulkan_instance.h
  vulkan_index_buffer.cc
  vulkan_index_buffer.h
  vulkan_memory.cc
  vulkan_memory.h
  vulkan_pipeline_builder.cc
  vulkan_pipeline_builder.h
  vulkan_presenter.cc
  vulkan_presenter.h
  vulkan_render_pass_cache.cc
  vulkan_render_pass_cache.h
  vulkan_render_target.cc
  vulkan_render_target.h
  vulkan_renderer.cc
  vulkan_renderer.h
  vulkan_texture.cc
  vulkan_texture.h
  vulkan_uploader.cc
  vulkan_uploader.h
  vulkan_vertex_buffer.cc
  vulkan_vertex_buffer.h
)

SET(EDGE_GLES_SOURCES
  gles_buffer.cc
  gles_buffer.h
  gles_context.cc
  gles_context.h
  gles_draw.cc
  gles_gpu_timer.cc
  gles_gpu_timer.h
  gles_render_target.cc
  gles_render_target.h
  gles_renderer.cc
  gles_renderer.h
  gles_texture.cc
  gles_texture.h
)

SET(EDGE_METAL_SOURCES
  metal_buffer.h
  metal_buffer.mm
  metal_context.h
  metal_context.mm
  metal_draw.mm
  metal_gpu_timer.h
  metal_gpu_timer.mm
  metal_impl.h
  metal_render_target.h
  metal_render_target.mm
  metal_renderer.h
  metal_renderer.mm
  metal_renderer_impl.h
  metal_texture.h
  metal_texture.mm
)

# what needs SDL3 is not edge's but edge_sdl's (below)
SET(EDGE_GLES_SDL_SOURCES
  gles_sdl_context.cc
  gles_sdl_context.h
)
SET(EDGE_METAL_SDL_SOURCES
  metal_sdl_surface.h
  metal_sdl_surface.mm
)
SET(EDGE_VULKAN_SDL_SOURCES
  vulkan_sdl_surface.cc
  vulkan_sdl_surface.h
)

SET(EDGE_SOURCES ${EDGE_CORE_SOURCES})
IF(EDGE_USE_VULKAN)
  LIST(APPEND EDGE_SOURCES ${EDGE_VULKAN_SOURCES})
ENDIF()
IF(EDGE_USE_GLES)
  LIST(APPEND EDGE_SOURCES ${EDGE_GLES_SOURCES})
ENDIF()
IF(EDGE_USE_METAL)
  LIST(APPEND EDGE_SOURCES ${EDGE_METAL_SOURCES})
  # ARC, as the backend is written
  SET_SOURCE_FILES_PROPERTIES(${EDGE_METAL_SOURCES} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
ENDIF()
# The shaders as SPIR-V for either renderer: Vulkan's own, and what the GLES renderer's are translated from (a browser's build has no Vulkan,
# and its shaders are these too).  glslangValidator is the machine's that builds.
IF(EDGE_USE_VULKAN OR EDGE_USE_GLES OR EDGE_USE_METAL)
  FIND_PROGRAM(GLSLANG_VALIDATOR glslangValidator)
  IF(NOT GLSLANG_VALIDATOR)
    MESSAGE(FATAL_ERROR "glslangValidator is required to build the shaders")
  ENDIF()

  SET(VK_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
  # edge.glsl, the bindings and push constants every shader drawn through
  # edge declares: an application's shaders include it with
  # glslangValidator -I${EDGE_SHADER_INCLUDE_DIR} (and depend on
  # ${EDGE_SHADER_INCLUDE_DIR}/edge.glsl).
  SET(EDGE_SHADER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "edge.glsl's directory")
  SET(VK_SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders/vulkan)
  # edge's own SPIR-V, which a game ships beside its binary (EDGE_VK_SHADER_DIR names it at run time); vulkan_spirv builds it
  SET(EDGE_SPIRV_DIR ${VK_SHADER_BINARY_DIR} CACHE INTERNAL "edge's SPIR-V")

  SET(VK_SHADER_OUTPUTS
    ${VK_SHADER_BINARY_DIR}/vertex.spv
    ${VK_SHADER_BINARY_DIR}/vertex_blend.spv
    ${VK_SHADER_BINARY_DIR}/vertex_skin.spv
    ${VK_SHADER_BINARY_DIR}/fragment.spv
    ${VK_SHADER_BINARY_DIR}/copy_vertex.spv
    ${VK_SHADER_BINARY_DIR}/copy_fragment.spv
    ${VK_SHADER_BINARY_DIR}/ui_vertex.spv
    ${VK_SHADER_BINARY_DIR}/ui_fragment.spv
  )

  ADD_CUSTOM_COMMAND(
    OUTPUT ${VK_SHADER_BINARY_DIR}/vertex.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VK_SHADER_BINARY_DIR}
    COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.0 -S vert
      ${VK_SHADER_SOURCE_DIR}/vertex.glsl
      -o ${VK_SHADER_BINARY_DIR}/vertex.spv
    DEPENDS ${VK_SHADER_SOURCE_DIR}/vertex.glsl ${VK_SHADER_SOURCE_DIR}/edge.glsl ${VK_SHADER_SOURCE_DIR}/edge_instance.glsl
    VERBATIM
  )

  ADD_CUSTOM_COMMAND(
    OUTPUT ${VK_SHADER_BINARY_DIR}/vertex_blend.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VK_SHADER_BINARY_DIR}
    COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.0 -S vert
      ${VK_SHADER_SOURCE_DIR}/vertex_blend.glsl
      -o ${VK_SHADER_BINARY_DIR}/vertex_blend.spv
    DEPENDS ${VK_SHADER_SOURCE_DIR}/vertex_blend.glsl ${VK_SHADER_SOURCE_DIR}/edge.glsl ${VK_SHADER_SOURCE_DIR}/edge_instance.glsl
    VERBATIM
  )

  ADD_CUSTOM_COMMAND(
    OUTPUT ${VK_SHADER_BINARY_DIR}/vertex_skin.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VK_SHADER_BINARY_DIR}
    COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.0 -S vert
      ${VK_SHADER_SOURCE_DIR}/vertex_skin.glsl
      -o ${VK_SHADER_BINARY_DIR}/vertex_skin.spv
    DEPENDS ${VK_SHADER_SOURCE_DIR}/vertex_skin.glsl ${VK_SHADER_SOURCE_DIR}/edge.glsl ${VK_SHADER_SOURCE_DIR}/edge_skin.glsl ${VK_SHADER_SOURCE_DIR}/edge_instance.glsl
    VERBATIM
  )

  ADD_CUSTOM_COMMAND(
    OUTPUT ${VK_SHADER_BINARY_DIR}/fragment.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VK_SHADER_BINARY_DIR}
    COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.0 -S frag
      ${VK_SHADER_SOURCE_DIR}/fragment.glsl
      -o ${VK_SHADER_BINARY_DIR}/fragment.spv
    DEPENDS ${VK_SHADER_SOURCE_DIR}/fragment.glsl ${VK_SHADER_SOURCE_DIR}/edge.glsl
    VERBATIM
  )

  # FullscreenCopy's (copy_*) and ImGuiRenderer's (ui_*)
  FOREACH(shader copy_vertex copy_fragment ui_vertex ui_fragment)
    STRING(REGEX REPLACE ".*_(vert|frag).*" "\\1" stage ${shader})
    ADD_CUSTOM_COMMAND(
      OUTPUT ${VK_SHADER_BINARY_DIR}/${shader}.spv
      COMMAND ${CMAKE_COMMAND} -E make_directory ${VK_SHADER_BINARY_DIR}
      COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.0 -S ${stage}
        ${VK_SHADER_SOURCE_DIR}/${shader}.glsl
        -o ${VK_SHADER_BINARY_DIR}/${shader}.spv
      DEPENDS ${VK_SHADER_SOURCE_DIR}/${shader}.glsl ${VK_SHADER_SOURCE_DIR}/edge.glsl
      VERBATIM
    )
  ENDFOREACH()

  ADD_CUSTOM_TARGET(vulkan_spirv DEPENDS ${VK_SHADER_OUTPUTS})
  # what edge_bundle_runtime (EdgeRuntime.cmake) copies beside a game's binary, and the targets that make it
  SET(EDGE_RUNTIME_SHADERS ${VK_SHADER_OUTPUTS} CACHE INTERNAL "edge's shaders a game ships")
  SET(EDGE_RUNTIME_SHADER_TARGETS vulkan_spirv CACHE INTERNAL "the targets that make EDGE_RUNTIME_SHADERS")

  # the same shaders as GLSL ES for the OpenGL ES renderer, and what it binds for each
  IF(EDGE_USE_GLES)
    INCLUDE(EdgeGles)
    SET(GLES_SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders/gles)
    # its translations for the GLES renderer; gles_shaders builds them
    SET(EDGE_GLES_SPIRV_DIR ${GLES_SHADER_BINARY_DIR} CACHE INTERNAL "edge's shaders as GLSL ES")
    EDGE_GLES_SHADERS(${GLES_SHADER_BINARY_DIR} GLES_SHADER_OUTPUTS ${VK_SHADER_OUTPUTS})
    ADD_CUSTOM_TARGET(gles_shaders DEPENDS ${GLES_SHADER_OUTPUTS})
    SET(EDGE_RUNTIME_SHADERS ${VK_SHADER_OUTPUTS} ${GLES_SHADER_OUTPUTS} CACHE INTERNAL "edge's shaders a game ships")
    SET(EDGE_RUNTIME_SHADER_TARGETS vulkan_spirv gles_shaders CACHE INTERNAL "the targets that make EDGE_RUNTIME_SHADERS")
    SET(EDGE_USE_GLES_SHADERS TRUE CACHE INTERNAL "edge translates its shaders for OpenGL ES (and a game can its own)")
  ENDIF()

  # the same shaders as Metal libraries (shader_cross --msl): metal_shaders builds them for the sdk the
  # renderer is built for (EDGE_METAL_SDK; a game that builds for iOS gives iphoneos or iphonesimulator)
  IF(EDGE_USE_METAL)
    INCLUDE(EdgeMetal)
    SET(METAL_SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders/metal)
    SET(EDGE_METAL_SPIRV_DIR ${METAL_SHADER_BINARY_DIR} CACHE INTERNAL "edge's shaders as Metal")
    IF(NOT EDGE_METAL_SDK)
      SET(EDGE_METAL_SDK macosx)
    ENDIF()
    SET(EDGE_METAL_SDK ${EDGE_METAL_SDK} CACHE INTERNAL "the sdk the Metal shader libraries are built for")
    EDGE_METAL_SHADERS(${METAL_SHADER_BINARY_DIR} METAL_SHADER_OUTPUTS ${EDGE_METAL_SDK} ${VK_SHADER_OUTPUTS})
    ADD_CUSTOM_TARGET(metal_shaders ALL DEPENDS ${METAL_SHADER_OUTPUTS})
    SET(EDGE_USE_METAL_SHADERS TRUE CACHE INTERNAL "edge translates its shaders for Metal (and a game can its own)")
  ENDIF()

  # Installed, every set is in share/edge/shaders (where shader_dirs.h looks from bin/ and lib/), and edge.glsl with what a shader
  # may include beside it in share/edge/glsl (edge_add_shaders of an installed edge); shader_cross, which translates a game's own
  # shaders, is bin/edge_shader_cross.
  # A wheel's (scikit-build-core): beside the module in the pyedge package, shaders/edge, where shader_dirs.h looks from the module.
  FILE(GLOB EDGE_SHADER_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/edge*.glsl)
  IF(SKBUILD AND EDGE_PYTHON)
    INSTALL(FILES ${EDGE_RUNTIME_SHADERS} ${METAL_SHADER_OUTPUTS} DESTINATION pyedge/shaders/edge)
    # (edge.glsl and what it includes: without them a pip user cannot compile a shader for load_shader_program)
    INSTALL(FILES ${EDGE_SHADER_INCLUDES} DESTINATION pyedge/glsl)
  ENDIF()
  IF(EDGE_INSTALL)
    INSTALL(FILES ${EDGE_RUNTIME_SHADERS} ${METAL_SHADER_OUTPUTS} DESTINATION ${CMAKE_INSTALL_DATADIR}/edge/shaders)
    INSTALL(FILES ${EDGE_SHADER_INCLUDES} DESTINATION ${CMAKE_INSTALL_DATADIR}/edge/glsl)
    IF(EDGE_USE_GLES OR EDGE_USE_METAL)
      INSTALL(PROGRAMS ${EDGE_SHADER_CROSS} DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME edge_shader_cross${CMAKE_EXECUTABLE_SUFFIX})
    ENDIF()
  ENDIF()
ENDIF()


# edge::pipeline_hash() (pipeline.h): the hash of what decides a .edge file's bytes -- edge's sources but for drawing and the bindings,
# the converter, the CMake lists (they pin the libraries fetched), the patches, rove (its sources, or an installed rove's headers) -- worked out by every build and written only when
# it changes (etc/cmake/edge_pipeline.cmake), so a source left unhashed is a model that is not made again.
SET(EDGE_PIPELINE_SOURCES ${EDGE_CORE_SOURCES} ${EDGE_ASSET_SOURCES})
LIST(FILTER EDGE_PIPELINE_SOURCES EXCLUDE REGEX "^(debug_lines|dynamic_mesh|fullscreen_copy|font|camera|scheduler|draw_call|uber_draw_call|lod_choice|motion|shader_dirs)\\.")
LIST(TRANSFORM EDGE_PIPELINE_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)
FILE(GLOB EDGE_PIPELINE_ROVE ${EDGE_ROVE_HASHED_DIR}/*.h ${EDGE_ROVE_HASHED_DIR}/*.cc)
LIST(FILTER EDGE_PIPELINE_ROVE EXCLUDE REGEX "/bind_[^/]*$")
FILE(GLOB EDGE_PIPELINE_PATCHES ${EDGE_ROOT}/etc/patches/*)
LIST(APPEND EDGE_PIPELINE_SOURCES ${EDGE_PIPELINE_ROVE} ${EDGE_PIPELINE_PATCHES} ${EDGE_ROOT}/tools/edge_convert.cc ${EDGE_ROOT}/CMakeLists.txt
  ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
LIST(JOIN EDGE_PIPELINE_SOURCES "\n" EDGE_PIPELINE_LIST)
FILE(WRITE ${CMAKE_CURRENT_BINARY_DIR}/edge_pipeline_sources.txt "${EDGE_PIPELINE_LIST}\n")
SET(EDGE_PIPELINE_CC ${CMAKE_CURRENT_BINARY_DIR}/edge_pipeline.cc)
ADD_CUSTOM_TARGET(edge_pipeline_hash
  COMMAND ${CMAKE_COMMAND} -DLIST=${CMAKE_CURRENT_BINARY_DIR}/edge_pipeline_sources.txt -DROOT=${EDGE_ROOT} -DOUT=${EDGE_PIPELINE_CC}
          -P ${EDGE_CMAKE_DIR}/edge_pipeline.cmake
  BYPRODUCTS ${EDGE_PIPELINE_CC}
  COMMENT "edge's pipeline hash"
  VERBATIM)
LIST(APPEND EDGE_SOURCES ${EDGE_PIPELINE_CC})

ADD_LIBRARY(edge STATIC ${EDGE_SOURCES})
ADD_DEPENDENCIES(edge edge_pipeline_hash)
SET_TARGET_PROPERTIES(edge PROPERTIES POSITION_INDEPENDENT_CODE ON)
# What a C++ program built against edge needs to compile its headers the way edge was compiled: the include path, the
# definitions that change what the headers declare, and the libraries whose headers they include (rove, Vulkan,
# GLES).  The rest is edge's own business: the log (log.h) is edge's own, on the standard library, and the .edge file's cereal
# is inside edge (serialized_model_archive.h).
# include/: the public headers as <edge/x.h> (one-line shims to src/).  src/ stays on the path as well, for code that includes
# "x.h" directly.
# Installed, the headers themselves are include/edge/*.h, and only include/ is on the path.
TARGET_INCLUDE_DIRECTORIES(edge PUBLIC $<BUILD_INTERFACE:${EDGE_ROOT}/include> $<BUILD_INTERFACE:${EDGE_ROOT}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
TARGET_COMPILE_FEATURES(edge PUBLIC cxx_std_20)
TARGET_COMPILE_DEFINITIONS(edge PUBLIC
  $<$<BOOL:${EDGE_USE_VULKAN}>:EDGE_USE_VULKAN>
  $<$<BOOL:${EDGE_USE_GLES}>:EDGE_USE_GLES>
  $<$<BOOL:${EDGE_USE_METAL}>:EDGE_USE_METAL>)
TARGET_LINK_LIBRARIES(edge PUBLIC rove::rove)
# (not generator expressions: the package names these targets as it exports them, edge::gles_api)
IF(EDGE_USE_VULKAN)
  TARGET_LINK_LIBRARIES(edge PUBLIC Vulkan::Vulkan)
ENDIF()
IF(EDGE_USE_GLES)
  TARGET_LINK_LIBRARIES(edge PUBLIC edge_gles_api)
ENDIF()
IF(EDGE_USE_METAL)
  TARGET_LINK_LIBRARIES(edge PUBLIC edge_metal_api)
ENDIF()
# (stb is headers only, compiled into edge by stb_impl.cc: nothing of it is linked)
TARGET_LINK_LIBRARIES(edge PRIVATE fastgltf::fastgltf $<BUILD_INTERFACE:edge_stb> $<BUILD_INTERFACE:edge_cereal>)
ADD_LIBRARY(edge::edge ALIAS edge)
# Where edge's shaders are looked for at run time (shader_dirs.h): beside the binary, else where this build puts them; a wheel's build is
# gone when the wheel runs, so it names none.  (dladdr finds the binary: libdl where the C library has it apart.)
SET(EDGE_SHADER_DIR_DEFINITIONS)
IF(NOT SKBUILD)
  IF(EDGE_USE_VULKAN OR EDGE_USE_GLES OR EDGE_USE_METAL)
    LIST(APPEND EDGE_SHADER_DIR_DEFINITIONS EDGE_VK_SHADER_BUILD_DIR="${VK_SHADER_BINARY_DIR}")
  ENDIF()
  IF(EDGE_USE_GLES)
    LIST(APPEND EDGE_SHADER_DIR_DEFINITIONS EDGE_GLES_SHADER_BUILD_DIR="${GLES_SHADER_BINARY_DIR}")
  ENDIF()
  IF(EDGE_USE_METAL)
    LIST(APPEND EDGE_SHADER_DIR_DEFINITIONS EDGE_METAL_SHADER_BUILD_DIR="${METAL_SHADER_BINARY_DIR}")
  ENDIF()
  IF(EDGE_VIEW_SHADER_BUILD_DIR)
    LIST(APPEND EDGE_SHADER_DIR_DEFINITIONS EDGE_VIEW_SHADER_BUILD_DIR="${EDGE_VIEW_SHADER_BUILD_DIR}")
  ENDIF()
ENDIF()
IF(EDGE_INSTALL)
  LIST(APPEND EDGE_SHADER_DIR_DEFINITIONS EDGE_SHADER_INSTALL_DIR="${CMAKE_INSTALL_FULL_DATADIR}/edge/shaders"
    EDGE_VIEW_SHADER_INSTALL_DIR="${CMAKE_INSTALL_FULL_DATADIR}/edge/view_shaders")
ENDIF()
SET_SOURCE_FILES_PROPERTIES(shader_dirs.cc PROPERTIES COMPILE_DEFINITIONS "${EDGE_SHADER_DIR_DEFINITIONS}")
TARGET_LINK_LIBRARIES(edge PRIVATE ${CMAKE_DL_LIBS})
IF(EDGE_USE_VULKAN OR EDGE_USE_GLES OR EDGE_USE_METAL)
  ADD_DEPENDENCIES(edge vulkan_spirv)
ENDIF()
IF(EDGE_USE_METAL)
  ADD_DEPENDENCIES(edge metal_shaders)
ENDIF()
IF(EDGE_USE_GLES)
  ADD_DEPENDENCIES(edge gles_shaders)
ENDIF()

# Dear ImGui drawn through edge: imgui_renderer.h, a header compiled against the program's ImGui, so nothing of ImGui is built or
# installed with edge.  Here, the parent's target imgui; installed, edge::imgui is edge::edge, and the program links its own ImGui
# (a package manager's, imgui::imgui, where edge was built with EDGE_SYSTEM_DEPS).
IF(EDGE_IMGUI)
  ADD_LIBRARY(edge_imgui INTERFACE)
  IF(EDGE_SYSTEM_DEPS)
    TARGET_LINK_LIBRARIES(edge_imgui INTERFACE edge imgui::imgui)
  ELSE()
    TARGET_LINK_LIBRARIES(edge_imgui INTERFACE edge $<BUILD_INTERFACE:imgui>)
  ENDIF()
  SET_TARGET_PROPERTIES(edge_imgui PROPERTIES EXPORT_NAME imgui)
  ADD_LIBRARY(edge::imgui ALIAS edge_imgui)
ENDIF()

# What needs SDL3: a host that makes its surface or context otherwise does not link it.
IF(EDGE_SDL)
  SET(EDGE_SDL_SOURCES)
  IF(EDGE_USE_VULKAN)
    LIST(APPEND EDGE_SDL_SOURCES ${EDGE_VULKAN_SDL_SOURCES})
  ENDIF()
  IF(EDGE_USE_GLES)
    LIST(APPEND EDGE_SDL_SOURCES ${EDGE_GLES_SDL_SOURCES})
  ENDIF()
  IF(EDGE_USE_METAL)
    LIST(APPEND EDGE_SDL_SOURCES ${EDGE_METAL_SDL_SOURCES})
    SET_SOURCE_FILES_PROPERTIES(${EDGE_METAL_SDL_SOURCES} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  ENDIF()
  ADD_LIBRARY(edge_sdl STATIC ${EDGE_SDL_SOURCES})
  SET_TARGET_PROPERTIES(edge_sdl PROPERTIES POSITION_INDEPENDENT_CODE ON)
  TARGET_LINK_LIBRARIES(edge_sdl PUBLIC edge SDL3::SDL3)
  SET_TARGET_PROPERTIES(edge_sdl PROPERTIES EXPORT_NAME sdl)
  ADD_LIBRARY(edge::sdl ALIAS edge_sdl)
ENDIF()

IF(EDGE_ASSET_TOOLS)
  ADD_LIBRARY(edge_asset STATIC ${EDGE_ASSET_SOURCES})
  SET_TARGET_PROPERTIES(edge_asset PROPERTIES POSITION_INDEPENDENT_CODE ON)
  TARGET_LINK_LIBRARIES(edge_asset PUBLIC edge PRIVATE meshoptimizer::meshoptimizer edge_bc7enc edge_astcenc edge_xatlas)
  SET_TARGET_PROPERTIES(edge_asset PROPERTIES EXPORT_NAME asset)
  ADD_LIBRARY(edge::asset ALIAS edge_asset)
ENDIF()

# FREE_THREADED: declares the module safe without the GIL on a free-threaded
# interpreter (python3.14t), which would otherwise turn the GIL back on for
# the whole process when it is imported; no effect on a GIL build.  Safe as
# edge's threads are (README: Threads): a renderer and what it made are used
# by one thread at a time, and renderers share nothing; nothing locks.
IF(EDGE_PYTHON)
  nanobind_add_module(pyedge FREE_THREADED ${EDGE_PYTHON_BINDINGS_SOURCE})
  SET_TARGET_PROPERTIES(pyedge PROPERTIES OUTPUT_NAME _pyedge)
  # a wheel (scikit-build-core, pyproject.toml) puts the module inside the pyedge package, which imports it from there
  IF(SKBUILD)
    INSTALL(TARGETS pyedge LIBRARY DESTINATION pyedge)
  ENDIF()
  TARGET_LINK_LIBRARIES(pyedge PRIVATE edge)
  # What pyedge links statically is its own: not symbols another library in the process (Blender's stb or fmt, say) can stand in for
  IF(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    TARGET_LINK_OPTIONS(pyedge PRIVATE -Wl,--exclude-libs,ALL)
  ENDIF()
  # (convert_gltf_to_edge: bind_model.cc)
  IF(EDGE_ASSET_TOOLS)
    TARGET_LINK_LIBRARIES(pyedge PRIVATE edge_asset)
    TARGET_COMPILE_DEFINITIONS(pyedge PRIVATE EDGE_PYEDGE_ASSET_TOOLS)
  ENDIF()
ENDIF()

# edge_convert: glTF to .edge, for a build to run over a game's models (tools/edge_convert.cc).
IF(EDGE_ASSET_TOOLS)
  ADD_EXECUTABLE(edge_convert ${EDGE_ROOT}/tools/edge_convert.cc)
  IF(SKBUILD)
    SET_TARGET_PROPERTIES(edge_convert PROPERTIES EXCLUDE_FROM_ALL ON)      # (a wheel ships pyedge alone)
  ENDIF()
  TARGET_LINK_LIBRARIES(edge_convert edge_asset)
  SET_TARGET_PROPERTIES(edge_convert PROPERTIES EXPORT_NAME convert)
  ADD_EXECUTABLE(edge::convert ALIAS edge_convert)
ENDIF()

# edge_shot: a model drawn once, to look at (tools/edge_shot.cc); its light's shader is built at the top (edge_view_shaders).  Its window, --gui,
# is there when edge_sdl is.
IF(EDGE_SHOT)
  IF(NOT (EDGE_USE_VULKAN OR EDGE_USE_METAL))
    MESSAGE(FATAL_ERROR "EDGE_SHOT: edge_shot draws with Vulkan or Metal, and this build has neither")
  ENDIF()
  ADD_EXECUTABLE(edge_shot ${EDGE_ROOT}/tools/edge_shot.cc)
  TARGET_LINK_LIBRARIES(edge_shot edge_asset)
  SET_TARGET_PROPERTIES(edge_shot PROPERTIES EXPORT_NAME shot)
  ADD_EXECUTABLE(edge::shot ALIAS edge_shot)
  IF(EDGE_SDL)
    TARGET_LINK_LIBRARIES(edge_shot edge_sdl)
    TARGET_COMPILE_DEFINITIONS(edge_shot PRIVATE EDGE_SHOT_GUI)
  ENDIF()
ENDIF()
