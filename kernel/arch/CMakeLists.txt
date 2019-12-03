# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

if (${KERNEL_SYSCC} OR ${USE_SYSCC})

   if (${ARCH} STREQUAL "i386")

      set(CMAKE_C_FLAGS "${ARCH_GCC_FLAGS}")
      set(CMAKE_CXX_FLAGS "${ARCH_GCC_FLAGS}")

      if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
         set(CMAKE_ASM_FLAGS "${ARCH_GCC_FLAGS}")
      else()
         # If system's compiler (CC) is Clang, we cannot use it for compiling
         # our assembly code, because LLVM's assembler (llvm-as) is not 100%
         # compatible with GAS's intel-syntax dialect.
         set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-gcc)
      endif()

   endif()

   set(TOOL_OBJCOPY "${OBJCOPY}")
   set(TOOL_STRIP "${STRIP}")

   if (KERNEL_FORCE_TC_ISYSTEM)
      include_directories(SYSTEM ${GCC_TC_ISYSTEM})
   endif()

else()
   set_cross_compiler()
endif()

configure_file(
   ${CMAKE_SOURCE_DIR}/scripts/templates/generate_coverage_report
   ${CMAKE_BINARY_DIR}/scripts/generate_kernel_coverage_report
   @ONLY
)

add_subdirectory(${ARCH})
