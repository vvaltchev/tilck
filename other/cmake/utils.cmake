# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

set(SPACE " ")

function(JOIN VALUES GLUE OUTPUT)
  string(REPLACE ";" "${GLUE}" _TMP_STR "${VALUES}")
  set(${OUTPUT} "${_TMP_STR}" PARENT_SCOPE)
endfunction()

function(PREPEND var prefix)

   set(listVar "")

   foreach(f ${ARGN})
      list(APPEND listVar "${prefix}${f}")
   endforeach(f)

   set(${var} "${listVar}" PARENT_SCOPE)

endfunction(PREPEND)

macro(define_env_cache_str_var)

   if (NOT DEFINED ${ARGV0})

      if (NOT "$ENV{${ARGV0}}" STREQUAL "")
         set(${ARGV0} "$ENV{${ARGV0}}" CACHE INTERNAL "")
      else()
         set(${ARGV0} "${ARGV1}" CACHE INTERNAL "")
      endif()

   else()

      if ("$ENV{PERMISSIVE}" STREQUAL "")
         if (NOT "$ENV{${ARGV0}}" STREQUAL "")
            if (NOT "$ENV{${ARGV0}}" STREQUAL "${${ARGV0}}")

               set(msg "")
               string(CONCAT msg "Environment var ${ARGV0}='$ENV{${ARGV0}}' "
                                 "differs from cached value='${${ARGV0}}'. "
                                 "The whole build directory must be ERASED "
                                 "in order to change that.")

               message(FATAL_ERROR "\n${msg}")
            endif()
         endif()
      endif()

   endif()

endmacro()

macro(define_env_cache_bool_var)

   if (NOT DEFINED _CACHE_${ARGV0})

      if (NOT "$ENV{${ARGV0}}" STREQUAL "")
         set(_CACHE_${ARGV0} "$ENV{${ARGV0}}" CACHE INTERNAL "")
      else()
         set(_CACHE_${ARGV0} 0 CACHE INTERNAL "")
      endif()

   else()

      if (NOT "$ENV{${ARGV0}}" STREQUAL "")
         if (NOT "$ENV{${ARGV0}}" STREQUAL "${_CACHE_${ARGV0}}")

            set(msg "")
            string(CONCAT msg "Environment variable ${ARGV0}='$ENV{${ARGV0}}' "
                              "differs from cached value='${${ARGV0}}'. "
                              "The whole build directory must be ERASED "
                              "in order to change that.")

            message(FATAL_ERROR "\n${msg}")
         endif()
      endif()
   endif()

   if (_CACHE_${ARGV0})
      set(${ARGV0} 1)
   else()
      set(${ARGV0} 0)
   endif()

endmacro()

macro(set_cross_compiler_internal)

   set(CMAKE_C_COMPILER ${ARGV0}/${ARGV1}-linux-gcc)
   set(CMAKE_CXX_COMPILER ${ARGV0}/${ARGV1}-linux-g++)
   set(CMAKE_ASM_COMPILER ${ARGV0}/${ARGV1}-linux-gcc)
   set(CMAKE_OBJCOPY ${ARGV0}/${ARGV1}-linux-objcopy)
   set(CMAKE_STRIP ${ARGV0}/${ARGV1}-linux-strip)
   set(CMAKE_AR ${ARGV0}/${ARGV1}-linux-ar)
   set(CMAKE_RANLIB ${ARGV0}/${ARGV1}-linux-ranlib)
   set(TOOL_GCOV ${ARGV0}/${ARGV1}-linux-gcov)

endmacro()

macro(set_cross_compiler)

   if (USE_SYSCC)

      if (${ARCH} STREQUAL "i386")
         set(CMAKE_C_FLAGS "${ARCH_GCC_FLAGS}")
         set(CMAKE_CXX_FLAGS "${ARCH_GCC_FLAGS} ${KERNEL_CXX_FLAGS}")
         set(CMAKE_ASM_FLAGS "${ARCH_GCC_FLAGS}")
      else()
         # Assume that the system's compiler is already able to build for
         # the given target architecture without additional flags.
      endif()

      set(TOOL_GCOV "${GCOV}")

   else()

      if (${ARCH} STREQUAL "riscv64")
         set(CMAKE_C_FLAGS "${ARCH_GCC_FLAGS}")
         set(CMAKE_CXX_FLAGS "${ARCH_GCC_FLAGS} ${KERNEL_CXX_FLAGS}")
         set(CMAKE_ASM_FLAGS "${ARCH_GCC_FLAGS}")
      else()
         # Assume that the system's compiler is already able to build for
         # the given target architecture without additional flags.
      endif()

      # DEFAULT CASE: use our pre-built toolchain
      set_cross_compiler_internal(${GCC_TOOLCHAIN} ${ARCH_GCC_TC})

   endif()

endmacro()

macro(set_cross_compiler_userapps)

   if (USE_SYSCC)

      set(CMAKE_C_COMPILER "${CMAKE_BINARY_DIR}/scripts/musl-gcc")
      set(CMAKE_CXX_COMPILER "${CMAKE_BINARY_DIR}/scripts/musl-g++")
      set(CMAKE_ASM_COMPILER "${CMAKE_BINARY_DIR}/scripts/musl-gcc")
      set(TOOL_GCOV "${GCOV}")

   else()

      # DEFAULT CASE: use our pre-built toolchain

      set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-gcc)
      set(CMAKE_CXX_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-g++)
      set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-gcc)
      set(CMAKE_OBJCOPY ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-objcopy)
      set(CMAKE_STRIP ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-strip)
      set(CMAKE_AR ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-ar)
      set(CMAKE_RANLIB ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-ranlib)
      set(TOOL_GCOV ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-gcov)

   endif()

endmacro()

macro(set_cross_compiler_gtests)

   # This macro is used in case ARCH_GTESTS is ON.

   if (USE_SYSCC)

      # Special case: the user wants to use system's compiler to compile code
      # for the target architecture. Just use our basic set_cross_compiler()
      # macro.

      set_cross_compiler()

   else()

      # DEFAULT CASE: use our pre-built toolchain

      # We want to use a cross-compiler BUT it has to use glibc instead of
      # libmusl as libc.

      set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN_GLIBC}/${ARCH_GCC_TC}-linux-gcc)
      set(CMAKE_CXX_COMPILER ${GCC_TOOLCHAIN_GLIBC}/${ARCH_GCC_TC}-linux-g++)
      set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN_GLIBC}/${ARCH_GCC_TC}-linux-gcc)
      set(CMAKE_OBJCOPY ${GCC_TOOLCHAIN_GLIBC}/${ARCH_GCC_TC}-linux-objcopy)
      set(CMAKE_STRIP ${GCC_TOOLCHAIN_GLIBC}/${ARCH_GCC_TC}-linux-strip)
      set(CMAKE_AR ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-ar)
      set(CMAKE_RANLIB ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-ranlib)
      set(TOOL_GCOV ${GCC_TOOLCHAIN_GLIBC}/${ARCH_GCC_TC}-linux-gcov)

   endif()

endmacro()

#message("CMAKE_ASM_COMPILE_OBJECT: ${CMAKE_ASM_COMPILE_OBJECT}")
#message("CMAKE_C_LINK_EXECUTABLE: ${CMAKE_C_LINK_EXECUTABLE}")
#message("CMAKE_C_COMPILE_OBJECT: ${CMAKE_C_COMPILE_OBJECT}")
#message("CMAKE_C_LINK_FLAGS: ${CMAKE_C_LINK_FLAGS}")
#message("CMAKE_CXX_COMPILE_OBJECT: ${CMAKE_CXX_COMPILE_OBJECT}")


function(smart_config_file src dest)

   configure_file(
      ${src}
      ${dest}.tmp
      @ONLY
   )

   execute_process(

      COMMAND
         ${CMAKE_COMMAND} -E compare_files ${dest}.tmp ${dest}

      RESULT_VARIABLE
         NEED_UPDATE

      OUTPUT_QUIET
      ERROR_QUIET
   )

   if(NEED_UPDATE)

      execute_process(
         COMMAND ${CMAKE_COMMAND} -E rename ${dest}.tmp ${dest}
      )

   else()

      execute_process(
         COMMAND ${CMAKE_COMMAND} -E rm ${dest}.tmp
      )

   endif()

   set_source_files_properties(${dest} PROPERTIES GENERATED TRUE)

endfunction()
