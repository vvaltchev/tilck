# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

#
# Build and statically link a kernel module
#
# ARGV0: target
# ARGV1: module name
# ARGV2: special flag: If equal to "_noarch", don't build the arch code.
#
function(build_and_link_module target modname)

   set(variant "${ARGV2}")
   set(MOD_${modname}_SOURCES_GLOB "")

   # message(STATUS "build_module(${target} ${modname} ${variant})")

   list(
      APPEND MOD_${modname}_SOURCES_GLOB
      "${CMAKE_SOURCE_DIR}/modules/${modname}/*.c"
      "${CMAKE_SOURCE_DIR}/modules/${modname}/*.cpp"
   )

   if (NOT "${variant}" STREQUAL "_noarch")

      if (NOT "${variant}" STREQUAL "")
         message(FATAL_ERROR "Flag must be \"_noarch\" or empty.")
      endif()

      list(
         APPEND MOD_${modname}_SOURCES_GLOB
         "${CMAKE_SOURCE_DIR}/modules/${modname}/${ARCH}/*.c"
         "${CMAKE_SOURCE_DIR}/modules/${modname}/${ARCH_FAMILY}/*.c"
      )
   endif()

   file(GLOB MOD_${modname}_SOURCES ${MOD_${modname}_SOURCES_GLOB})

   # It's totally possible that some modules contain exclusively arch-only
   # code. In that case, the list of sources will be empty when the flag
   # "noarch" is passed and we just won't create any target.

   if (MOD_${modname}_SOURCES)

      add_library(
         mod_${modname}${variant} STATIC EXCLUDE_FROM_ALL
         ${MOD_${modname}_SOURCES}
      )

      if ("${variant}" STREQUAL "_noarch")

         set_target_properties(

            mod_${modname}${variant}

            PROPERTIES
               COMPILE_FLAGS "${KERNEL_NO_ARCH_FLAGS}"
         )

      else()

         set_target_properties(

            mod_${modname}${variant}

            PROPERTIES
               COMPILE_FLAGS "${KERNEL_FLAGS} ${ACTUAL_KERNEL_ONLY_FLAGS}"
         )

      endif()
      target_link_libraries(${target} mod_${modname}${variant})
   endif()

endfunction()

#
# Build and link all modules to a given target
#
function(build_all_modules TARGET_NAME)

   set(TARGET_VARIANT "${ARGV1}")
   target_link_libraries(${TARGET_NAME} -Wl,--whole-archive)

   foreach (mod ${modules_list})

      if (NOT MOD_${mod})
         continue()
      endif()

      if ("${TARGET_VARIANT}" STREQUAL "_noarch")
         list(FIND no_arch_modules_whitelist ${mod} _index)
         if (${_index} EQUAL -1)
            continue()
         endif()
      endif()

      if (EXISTS ${CMAKE_SOURCE_DIR}/modules/${mod}/${mod}.cmake)
         include(${CMAKE_SOURCE_DIR}/modules/${mod}/${mod}.cmake)
      else()
         build_and_link_module(${TARGET_NAME} ${mod} ${TARGET_VARIANT})
      endif()

   endforeach()

   target_link_libraries(${TARGET_NAME} -Wl,--no-whole-archive)
endfunction()
