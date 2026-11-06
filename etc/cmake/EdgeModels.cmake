# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# A game's models as .edge files, made by edge_convert at build time.  edge's CMakeLists includes this file, and an installed edge's
# package (edgeConfig.cmake); a project calls the functions after add_subdirectory(edge) or find_package(edge).
#
#   edge_convert_models(<target> SOURCE_DIR <dir> DESTINATION <dir> [OPTIONS <edge_convert option>...])
#
# Every .glb and .gltf under SOURCE_DIR becomes DESTINATION/<its path under SOURCE_DIR, without the extension>.edge, made with the
# OPTIONS (tools/edge_convert.cc); <target> makes them all, with every build (ALL), and <target>_OUTPUTS names them.
#
#   edge_convert_model(OUTPUT <model.edge> GLTF <file> [OPTIONS <edge_convert option>...])
#
# One model, for a project that gives its models options of their own; a target that depends on OUTPUT makes it.
#
# A model is made again when what it is made from changed by its bytes, not by its time (git-annex sync rewrites file times): the glTF,
# each file it names (edge_convert --inputs), the options and the pipeline edge_convert makes models with (--version; a converter linked
# anew with the same pipeline makes the same files), all recorded in <model.edge>.json beside the model.
# A changed file time runs the rule, which finds the record the same and only touches the model.

include_guard(GLOBAL)

set(EDGE_MODELS_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/edge_convert_model.cmake" CACHE INTERNAL "the script that makes one model")

function(edge_convert_model)
  cmake_parse_arguments(PARSE_ARGV 0 ARG "" "OUTPUT;GLTF" "OPTIONS")
  if(NOT ARG_OUTPUT OR NOT ARG_GLTF)
    message(FATAL_ERROR "edge_convert_model: OUTPUT and GLTF are needed")
  endif()
  if(ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "edge_convert_model: unknown arguments ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  # edge_convert: edge's own (edge added with add_subdirectory) or an imported one; edge::convert: an installed edge's
  if(TARGET edge_convert)
    set(converter edge_convert)
  elseif(TARGET edge::convert)
    set(converter edge::convert)
  else()
    message(FATAL_ERROR "edge_convert_model: there is no target edge_convert or edge::convert (add edge, or find_package(edge), first)")
  endif()
  get_filename_component(gltf "${ARG_GLTF}" ABSOLUTE)
  get_filename_component(model "${ARG_OUTPUT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
  get_filename_component(dir "${model}" DIRECTORY)
  get_filename_component(name "${model}" NAME)
  # (the options go to the script as one argument, | between them: none of edge_convert's has one)
  list(JOIN ARG_OPTIONS "|" options)
  add_custom_command(
    OUTPUT "${model}"
    COMMAND "${CMAKE_COMMAND}" "-DEDGE_CONVERT=$<TARGET_FILE:${converter}>" "-DOPTIONS=${options}" "-DGLTF=${gltf}"
            "-DMODEL=${model}" "-DRECORD=${model}.json" "-DDEPFILE=${dir}/${name}.d" -P "${EDGE_MODELS_SCRIPT}"
    DEPENDS "${gltf}" ${converter} "$<TARGET_FILE:${converter}>" "${EDGE_MODELS_SCRIPT}"
    DEPFILE "${dir}/${name}.d"
    COMMENT "edge_convert ${gltf}"
    VERBATIM)
endfunction()

function(edge_convert_models TARGET)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "SOURCE_DIR;DESTINATION" "OPTIONS")
  foreach(needed SOURCE_DIR DESTINATION)
    if(NOT ARG_${needed})
      message(FATAL_ERROR "edge_convert_models(${TARGET}): ${needed} is needed")
    endif()
  endforeach()
  if(ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "edge_convert_models(${TARGET}): unknown arguments ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT IS_DIRECTORY "${ARG_SOURCE_DIR}")
    message(FATAL_ERROR "edge_convert_models(${TARGET}): SOURCE_DIR ${ARG_SOURCE_DIR} is not a directory")
  endif()
  file(GLOB_RECURSE sources CONFIGURE_DEPENDS RELATIVE "${ARG_SOURCE_DIR}" "${ARG_SOURCE_DIR}/*.glb" "${ARG_SOURCE_DIR}/*.gltf")
  set(outputs)
  foreach(source ${sources})
    string(REGEX REPLACE "\\.(glb|gltf)$" ".edge" model "${source}")
    edge_convert_model(OUTPUT "${ARG_DESTINATION}/${model}" GLTF "${ARG_SOURCE_DIR}/${source}" OPTIONS ${ARG_OPTIONS})
    list(APPEND outputs "${ARG_DESTINATION}/${model}")
  endforeach()
  add_custom_target(${TARGET} ALL DEPENDS ${outputs})
  set(${TARGET}_OUTPUTS ${outputs} PARENT_SCOPE)
endfunction()
