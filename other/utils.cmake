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

   set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-gcc)
   set(CMAKE_CXX_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-g++)
   set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-gcc)
   set(OBJCOPY ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-objcopy)
   set(STRIP ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-strip)

endmacro()

# For the moment this macro is the same as set_cross_compiler()
macro(set_cross_compiler_userapps)

   set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-gcc)
   set(CMAKE_CXX_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-g++)
   set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-gcc)
   set(OBJCOPY ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-objcopy)
   set(STRIP ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-strip)

endmacro()

# In case ARCH_GTESTS is ON, we want to use a cross-compiler BUT it has to use
# glibc instead of musl by default.
macro(set_cross_compiler_gtests)

   set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN_GLIBC}/${ARCH_GCC_TC}-linux-gcc)
   set(CMAKE_CXX_COMPILER ${GCC_TOOLCHAIN_GLIBC}/${ARCH_GCC_TC}-linux-g++)
   set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN_GLIBC}/${ARCH_GCC_TC}-linux-gcc)
   set(OBJCOPY ${GCC_TOOLCHAIN_GLIBC}/${ARCH_GCC_TC}-linux-objcopy)
   set(STRIP ${GCC_TOOLCHAIN_GLIBC}/${ARCH_GCC_TC}-linux-strip)

endmacro()

#message("CMAKE_ASM_COMPILE_OBJECT: ${CMAKE_ASM_COMPILE_OBJECT}")
#message("CMAKE_C_LINK_EXECUTABLE: ${CMAKE_C_LINK_EXECUTABLE}")
#message("CMAKE_C_COMPILE_OBJECT: ${CMAKE_C_COMPILE_OBJECT}")
#message("CMAKE_C_LINK_FLAGS: ${CMAKE_C_LINK_FLAGS}")
#message("CMAKE_CXX_COMPILE_OBJECT: ${CMAKE_CXX_COMPILE_OBJECT}")
