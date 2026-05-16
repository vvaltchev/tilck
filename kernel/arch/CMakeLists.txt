# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

if (${KERNEL_SYSCC} AND ${ARCH} STREQUAL "i386"
                    AND NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")

   # Clang does not accept -march=i686; use --target instead.
   set(CMAKE_C_FLAGS "--target=i686-linux-gnu")
   set(CMAKE_CXX_FLAGS "--target=i686-linux-gnu ${KERNEL_CXX_FLAGS}")
   # Clang's integrated assembler (llvm-as) is not 100% compatible
   # with GAS's intel-syntax dialect, so use the GCC toolchain's as.
   set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-gcc)

else()

   # For both SYSCC (GCC) and non-SYSCC (toolchain) builds, apply ARCH_GCC_FLAGS.
   # The old set_cross_compiler() macro did this; now that toolchain files only
   # set compiler paths, we must set the flags here.
   set(CMAKE_C_FLAGS "${ARCH_GCC_FLAGS}")
   set(CMAKE_CXX_FLAGS "${ARCH_GCC_FLAGS} ${KERNEL_CXX_FLAGS}")
   set(CMAKE_ASM_FLAGS "${ARCH_GCC_FLAGS}")

endif()

#
# When using a system compiler (KERNEL_SYSCC), point the kernel's
# include search at the cross-toolchain's musl headers via -isystem
# in two cases:
#
#   - KERNEL_FORCE_TC_ISYSTEM: explicit opt-in used by the
#     clang_tc_isystem static-analysis generator on Linux.
#
#   - Host is darwin: required, not opt-in. The host clang invoked
#     with --target=<arch>-linux-* doesn't have access to a Linux
#     sysroot, so its bundled `#include_next` chains for
#     <inttypes.h>, <sys/types.h>, etc. resolve to nothing. The
#     toolchain's musl headers fill that gap. (See the
#     GCC_TC_ISYSTEM override in the top-level CMakeLists.txt
#     for the matching path adjustment.)
#
if (${KERNEL_SYSCC} AND (KERNEL_FORCE_TC_ISYSTEM OR
                          CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin"))
   include_directories(SYSTEM ${GCC_TC_ISYSTEM})
endif()

smart_config_file(
   ${PROJ_ROOT}/scripts/templates/generate_coverage_report
   ${BUILD_DIR}/scripts/generate_kernel_coverage_report
)

add_subdirectory(${ARCH})
