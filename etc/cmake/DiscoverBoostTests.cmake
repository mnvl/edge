# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# Run after a Boost.Test binary is built: writes a ctest include file with one test per top-level suite (or case), so `ctest -j` runs
# them in parallel.  The suites of SKIP (a ;-list) are left out -- the ones that time something, which are slow and fail under load,
# and which a target of their own runs -- and each must exist, so that a rename does not drop one silently.  The suites of SERIAL
# (a ;-list) are marked RUN_SERIAL: they are quick alone but share one GPU, so `ctest -j` running many at once pushes them over their
# timeout; each must exist too.  With WORKDIR the tests run in that directory.
# Usage: cmake -DEXE=<binary> -DOUT=<file> [-DWORKDIR=<dir>] [-DSKIP="a;b"] [-DSERIAL="a;b"] -P this
cmake_minimum_required(VERSION 3.20)   # (a script run with -P has the policies of no project: IN_LIST)
execute_process(COMMAND ${EXE} --list_content RESULT_VARIABLE status ERROR_VARIABLE listing OUTPUT_VARIABLE out)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "${EXE} --list_content failed (${status}): ${listing}${out}")
endif()
set(run "\"${EXE}\"")
if(WORKDIR)
  set(run "\"${CMAKE_COMMAND}\" \"-E\" \"chdir\" \"${WORKDIR}\" \"${EXE}\"")
endif()
string(REPLACE "\n" ";" lines "${listing}")
set(script "")
set(skipped "")
set(serialized "")
foreach(line IN LISTS lines)
  if(line MATCHES "^([^ ]+)\\*$" AND CMAKE_MATCH_1 IN_LIST SKIP)
    list(APPEND skipped ${CMAKE_MATCH_1})
  elseif(line MATCHES "^([^ ]+)\\*$")
    string(APPEND script "add_test(\"${CMAKE_MATCH_1}\" ${run} \"--run_test=${CMAKE_MATCH_1}\")\n")
    if(CMAKE_MATCH_1 IN_LIST SERIAL)
      string(APPEND script "set_tests_properties(\"${CMAKE_MATCH_1}\" PROPERTIES RUN_SERIAL TRUE)\n")
      list(APPEND serialized ${CMAKE_MATCH_1})
    endif()
  elseif(NOT line STREQUAL "" AND NOT line MATCHES "^ ")
    message(FATAL_ERROR "unexpected line in --list_content (a disabled test?): ${line}")
  endif()
endforeach()
foreach(name IN LISTS SKIP)
  if(NOT name IN_LIST skipped)
    message(FATAL_ERROR "${EXE} has no suite ${name}, which DiscoverBoostTests.cmake was told to skip")
  endif()
endforeach()
foreach(name IN LISTS SERIAL)
  if(NOT name IN_LIST serialized)
    message(FATAL_ERROR "${EXE} has no suite ${name}, which DiscoverBoostTests.cmake was told to run serially")
  endif()
endforeach()
if(script STREQUAL "")
  message(FATAL_ERROR "--list_content of ${EXE} listed no tests")
endif()
file(WRITE ${OUT} "${script}")
