# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

file(GLOB children RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
     ${GLOB_CONF_DEP}
     ${CMAKE_CURRENT_SOURCE_DIR}/*)

set(EXTRA_APPS_LIST "")

foreach(child ${children})
   if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${child})
      message(STATUS "Adding EXTRA user apps directory '${child}'")
      add_subdirectory(${child})
   endif()
endforeach()

set(EXTRA_APPS_LIST ${EXTRA_APPS_LIST} PARENT_SCOPE)
