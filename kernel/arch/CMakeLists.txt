# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

if (${KERNEL_SYSCC})

   if (${ARCH} STREQUAL "i386")

      if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
         set(CMAKE_C_FLAGS "${ARCH_GCC_FLAGS}")
         set(CMAKE_CXX_FLAGS "${ARCH_GCC_FLAGS} ${KERNEL_CXX_FLAGS}")
         set(CMAKE_ASM_FLAGS "${ARCH_GCC_FLAGS}")
      else()
         # Clang does not accept -march=i686; use --target instead.
         set(CMAKE_C_FLAGS "--target=i686-linux-gnu")
         set(CMAKE_CXX_FLAGS "--target=i686-linux-gnu ${KERNEL_CXX_FLAGS}")
         # Clang's integrated assembler (llvm-as) is not 100% compatible
         # with GAS's intel-syntax dialect, so use the GCC toolchain's as.
         set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-gcc)
      endif()

   endif()

   if (KERNEL_FORCE_TC_ISYSTEM)
      include_directories(SYSTEM ${GCC_TC_ISYSTEM})
   endif()

else()
   # Do nothing
endif()

smart_config_file(
   ${PROJ_ROOT}/scripts/templates/generate_coverage_report
   ${BUILD_DIR}/scripts/generate_kernel_coverage_report
)

add_subdirectory(${ARCH})
