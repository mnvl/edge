# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# The pipeline hash: the SHA-256 of the sources that decide what a .edge file holds (edge's, but its GPU backends', bindings' and the
# like, and rove's), so that a model made by another pipeline is made again and one made by the same is not, however often edge_convert
# is linked anew.  Writes <OUT>, a source of edge::pipeline_hash(), only when the hash changed (what includes it is not rebuilt).
#
#   cmake -DLIST=<file of the sources, one a line> -DROOT=<dir the names are relative to> -DOUT=<edge_pipeline.cc> -P edge_pipeline.cmake

foreach(needed LIST ROOT OUT)
  if(NOT DEFINED ${needed} OR "${${needed}}" STREQUAL "")
    message(FATAL_ERROR "edge_pipeline.cmake: -D${needed}= is needed")
  endif()
endforeach()

file(STRINGS "${LIST}" sources)
list(SORT sources)
set(digests "")
foreach(source ${sources})
  if(NOT EXISTS "${source}")
    message(FATAL_ERROR "edge_pipeline.cmake: ${source} is not there")
  endif()
  file(SHA256 "${source}" digest)
  file(RELATIVE_PATH name "${ROOT}" "${source}")
  string(APPEND digests "${name} ${digest}\n")
endforeach()
string(SHA256 hash "${digests}")
string(SUBSTRING "${hash}" 0 16 hash)

set(text "// made by etc/cmake/edge_pipeline.cmake: the hash of the sources that decide what a .edge file holds\n#include \"pipeline.h\"\n\nchar const *edge::pipeline_hash()\n{\n\treturn \"${hash}\";\n}\n")
if(EXISTS "${OUT}")
  file(READ "${OUT}" old)
  if(old STREQUAL text)
    return()
  endif()
endif()
file(WRITE "${OUT}" "${text}")
