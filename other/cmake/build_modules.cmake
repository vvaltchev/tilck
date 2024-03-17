# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

set(TOOL_WS ${CMAKE_BINARY_DIR}/scripts/weaken_syms)

#
# Internal macro use by the build_and_link_module() function
#

macro(__build_and_link_module_patch_logic)

   set(PATCHED_MOD_FILE "libmod_${modname}_patched.a")

   add_custom_command(

      OUTPUT
         ${PATCHED_MOD_FILE}
      COMMAND
         cp libmod_${modname}${variant}.a ${PATCHED_MOD_FILE}
      COMMAND
         ${TOOL_WS} ${PATCHED_MOD_FILE} ${WRAPPED_SYMS}
      DEPENDS
         mod_${modname}${variant}
         ${TOOL_WS}
         elfhack32
         elfhack64
      COMMENT
         "Patching the module ${modname} to allow wrapping of symbols"
      VERBATIM
   )

   add_custom_target(
      mod_${modname}_patched
      DEPENDS ${PATCHED_MOD_FILE}
   )

endmacro()

#
# Build and statically link a kernel module
#
# ARGV0: target
# ARGV1: module name
# ARGV2: special flag: If equal to "_noarch", don't build the arch code.
# ARGV3: patch flag: run the WS_TOOL to weaken all the symbols in the static
#        archive if the flag is true.
#

function(build_and_link_module target modname)

   set(variant "${ARGV2}")
   set(DO_PATCH "${ARGV3}")
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

   file(
      GLOB
      MOD_${modname}_SOURCES         # Output variable
      ${GLOB_CONF_DEP}               # The CONFIGURE_DEPENDS option
      ${MOD_${modname}_SOURCES_GLOB} # The input GLOB text
   )

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

         if (DO_PATCH)
            __build_and_link_module_patch_logic()
         endif(DO_PATCH)

      else()

         set_target_properties(

            mod_${modname}${variant}

            PROPERTIES
               COMPILE_FLAGS "${KERNEL_FLAGS} ${ACTUAL_KERNEL_ONLY_FLAGS}"
         )

      endif()

      # Link the patched or the regular module version

      if (DO_PATCH)

         add_dependencies(${target} mod_${modname}_patched)

         target_link_libraries(
            ${target} ${CMAKE_CURRENT_BINARY_DIR}/${PATCHED_MOD_FILE}
         )

      else()
         target_link_libraries(${target} mod_${modname}${variant})
      endif()

   endif(MOD_${modname}_SOURCES)
endfunction()

#
# Build and link all modules to a given target
#
function(build_all_modules TARGET_NAME)

   set(TARGET_VARIANT "${ARGV1}")
   set(DO_PATCH "${ARGV2}")
   target_link_libraries(${TARGET_NAME} -Wl,--whole-archive)

   foreach (mod ${modules_list})

      if ("${TARGET_VARIANT}" STREQUAL "_noarch")
         list(FIND no_arch_modules_whitelist ${mod} _index)
         if (${_index} EQUAL -1)
            continue()
         endif()
      else()
         # Even if it's ugly, check here if the module should be compiled-in
         # or not. In the "noarch" case, always compile the modules in, because
         # they are needed for unit tests.
         if (NOT MOD_${mod})
            continue()
         endif()
      endif()

      if (EXISTS ${CMAKE_SOURCE_DIR}/modules/${mod}/${mod}.cmake)

         # Use the custom per-module CMake file
         include(${CMAKE_SOURCE_DIR}/modules/${mod}/${mod}.cmake)

      else()

         # Use the generic build & link code
         build_and_link_module(
            ${TARGET_NAME} ${mod} ${TARGET_VARIANT} ${DO_PATCH}
         )

      endif()

   endforeach()

   target_link_libraries(${TARGET_NAME} -Wl,--no-whole-archive)
endfunction()
