# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

set(SPACE " ")

function(JOIN VALUES GLUE OUTPUT)
  string (REPLACE ";" "${GLUE}" _TMP_STR "${VALUES}")
  set (${OUTPUT} "${_TMP_STR}" PARENT_SCOPE)
endfunction()

function(PREPEND var prefix)
   set(listVar "")
   foreach(f ${ARGN})
      list(APPEND listVar "${prefix}${f}")
   endforeach(f)
   set(${var} "${listVar}" PARENT_SCOPE)
endfunction(PREPEND)

macro(set_cross_compiler)

   if (${ARCH} STREQUAL "i386")

      set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/i686-linux-gcc)
      set(CMAKE_CXX_COMPILER ${GCC_TOOLCHAIN}/i686-linux-g++)
      set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN}/i686-linux-gcc)
      set(OBJCOPY ${GCC_TOOLCHAIN}/i686-linux-objcopy)
      set(STRIP ${GCC_TOOLCHAIN}/i686-linux-strip)

   else()

      message(FATAL_ERROR "Architecture '${ARCH}' not supported.")

   endif()

endmacro()

macro(set_cross_compiler_glibc)

   if (${ARCH} STREQUAL "i386")

      set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN_GLIBC}/i686-linux-gcc)
      set(CMAKE_CXX_COMPILER ${GCC_TOOLCHAIN_GLIBC}/i686-linux-g++)
      set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN_GLIBC}/i686-linux-gcc)
      set(OBJCOPY ${GCC_TOOLCHAIN_GLIBC}/i686-linux-objcopy)
      set(STRIP ${GCC_TOOLCHAIN_GLIBC}/i686-linux-strip)

   else()

      message(FATAL_ERROR "Architecture '${ARCH}' not supported.")

   endif()

endmacro()

#message("CMAKE_ASM_COMPILE_OBJECT: ${CMAKE_ASM_COMPILE_OBJECT}")
#message("CMAKE_C_LINK_EXECUTABLE: ${CMAKE_C_LINK_EXECUTABLE}")
#message("CMAKE_C_COMPILE_OBJECT: ${CMAKE_C_COMPILE_OBJECT}")
#message("CMAKE_C_LINK_FLAGS: ${CMAKE_C_LINK_FLAGS}")
#message("CMAKE_CXX_COMPILE_OBJECT: ${CMAKE_CXX_COMPILE_OBJECT}")
