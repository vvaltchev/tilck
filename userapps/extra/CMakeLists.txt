# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

# Override CMAKE_PROJECT_NAME for backwards compatibility with extra app
# projects that check: if (CMAKE_PROJECT_NAME STREQUAL "tilck")
set(CMAKE_PROJECT_NAME "tilck")

# Function for extra app projects to register their deliverables.
# Each call appends the binary's absolute path to the per-project manifest.
function(tilck_add_extra_app binary_path)
   file(APPEND "${TILCK_EXTRA_MANIFEST}" "${binary_path}\n")
endfunction()

file(GLOB children RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
     ${GLOB_CONF_DEP}
     ${CMAKE_CURRENT_SOURCE_DIR}/*)

foreach(child ${children})
   if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${child})

      # Check for an EXTRA_<NAME> toggle: skip if explicitly disabled
      string(TOUPPER "${child}" child_upper)
      set(TILCK_EXTRA_MANIFEST
         "${CMAKE_CURRENT_BINARY_DIR}/${child}/manifest.txt")

      if(DEFINED EXTRA_${child_upper} AND NOT EXTRA_${child_upper})
         message(STATUS "Skipping EXTRA user apps directory '${child}' (disabled)")
         file(REMOVE "${TILCK_EXTRA_MANIFEST}")
         continue()
      endif()

      # Clear the manifest file so it only contains current deliverables
      file(WRITE "${TILCK_EXTRA_MANIFEST}" "")

      message(STATUS "Adding EXTRA user apps directory '${child}'")
      add_subdirectory(${child})
   endif()
endforeach()
