# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# Makes <MODEL> from <GLTF> with edge_convert unless it was made from the same bytes (EdgeModels.cmake):
#
#   cmake -DEDGE_CONVERT=<exe> -DOPTIONS=<a>|<b>... -DGLTF=<glTF> -DMODEL=<edge> -DRECORD=<json> -DDEPFILE=<d> -P edge_convert_model.cmake
#
# RECORD holds what edge_convert says it is (--version: the .edge format and the pipeline that makes it, not its binary, which every link
# of edge makes anew), the options and the md5 of every file the model is made from; DEPFILE names those files for the build tool, so
# that a change to a texture runs this again.

foreach(needed EDGE_CONVERT GLTF MODEL RECORD DEPFILE)
  if(NOT DEFINED ${needed} OR "${${needed}}" STREQUAL "")
    message(FATAL_ERROR "edge_convert_model.cmake: -D${needed}= is needed")
  endif()
endforeach()
string(REPLACE "|" ";" options "${OPTIONS}")

execute_process(COMMAND "${EDGE_CONVERT}" --inputs "${GLTF}" RESULT_VARIABLE result OUTPUT_VARIABLE inputs ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "edge_convert --inputs ${GLTF} failed (${result}): ${error}")
endif()
string(REGEX REPLACE "\n$" "" inputs "${inputs}")
string(REPLACE "\n" ";" inputs "${inputs}")

execute_process(COMMAND "${EDGE_CONVERT}" --version RESULT_VARIABLE result OUTPUT_VARIABLE converter ERROR_VARIABLE error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT result EQUAL 0 OR converter STREQUAL "")
  message(FATAL_ERROR "edge_convert --version failed (${result}): ${error}")
endif()

# the record, and the depfile: "<model>: <input> <input>...", a space in a path escaped
string(REPLACE " " "\\ " depfile "${MODEL}:")
set(sources "")
foreach(input ${inputs})
  file(MD5 "${input}" md5)
  string(APPEND sources "    \"${input}\": \"${md5}\",\n")
  string(REPLACE " " "\\ " escaped "${input}")
  string(APPEND depfile " ${escaped}")
endforeach()
string(REGEX REPLACE ",\n$" "\n" sources "${sources}")
list(JOIN options "\", \"" quoted)
if(quoted STREQUAL "")
  set(quoted "[]")
else()
  set(quoted "[\"${quoted}\"]")
endif()
set(record "{\n  \"edge_convert\": \"${converter}\",\n  \"options\": ${quoted},\n  \"sources\": {\n${sources}  }\n}\n")
file(WRITE "${DEPFILE}" "${depfile}\n")

if(EXISTS "${MODEL}" AND EXISTS "${RECORD}")
  file(READ "${RECORD}" old)
  if(old STREQUAL record)
    file(TOUCH_NOCREATE "${MODEL}")   # newer than the inputs whose time changed: Make goes by it (Ninja's log remembers the run)
    return()
  endif()
endif()

# no record while the model is being made: a conversion that fails leaves none, so it is not taken for made from what it named
file(REMOVE "${RECORD}")
get_filename_component(dir "${MODEL}" DIRECTORY)
file(MAKE_DIRECTORY "${dir}")
execute_process(COMMAND "${EDGE_CONVERT}" ${options} "${GLTF}" "${MODEL}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "edge_convert failed (${result}) on ${GLTF}")
endif()
file(WRITE "${RECORD}" "${record}")
