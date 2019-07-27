# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

function (show_wrong_arch_error)

   set(msg "")
   string(CONCAT msg "Tilck has been designed to be built on x86_64 host "
                     "machines no matter which target architecture has been "
                     "chosen. Reason: the build system uses a toolchain "
                     "pre-compiled for x86_64 hosts. Current system CPU: "
                     "'${CMAKE_SYSTEM_PROCESSOR}'. However, in a *non* "
                     "official way, building on HOST_ARCH == ARCH is "
                     "supported, if USE_SYSCC, CC, CXX are set.")

   message(FATAL_ERROR "${msg}")

endfunction()

function (show_missing_use_syscc_error)

   set(msg "")
   string(CONCAT msg "In order to build Tilck on this host architecture "
                     "the env variables USE_SYSCC, CC and CXX *must be* set. "
                     "WARNING: this scenario is *not* supported in an official"
                     " way, even if it should work.")

   message(FATAL_ERROR "${msg}")

endfunction()

function (show_same_arch_build_warning)

   set(msg "")
   string(CONCAT msg "Building with HOST_ARCH == ARCH and USE_SYSCC=1 is not"
                     " supported in an official way")

   message(WARNING "${msg}")

endfunction()

function (show_no_musl_syscc_error)

   set(msg "")
   string(CONCAT msg "In order to build with USE_SYSCC=1 libmusl *must be* "
                     "in the toolchain. Run: ${BTC_SCRIPT} -s build_libmusl "
                     "first.")

   message(FATAL_ERROR "${msg}")

endfunction()

function (show_wconv_warning)

   set(msg "")
   string(APPEND msg "WCONV (-Wconversion) is supported ONLY when the "
                     "kernel is built with Clang: "
                     "(CC=clang and CMAKE_ARGS='-DKERNEL_SYSCC=1')")

   message(WARNING "${msg}")

endfunction()

function (show_clang_and_syscc_error)

   set(msg "")
   string(APPEND msg "USE_SYSCC=1 is not supported with Clang. "
                     "Please use GCC. Note[1]: it is possible to build just "
                     "the kernel with Clang by setting CC=clang the CMake "
                     "variable KERNEL_SYSCC=1. However, the i686 gcc "
                     "toolchain will still be used for assembly files and "
                     "other targets. Note[2]: if you're using "
                     "./scripts/cmake_run, you can set CMake variables by "
                     "setting the CMAKE_ARGS environment variable this way: "
                     "\nCMAKE_ARGS='-DCMAKE_VAR1=0 -DVAR2=blabla' "
                     "./scripts/cmake_run")

   message(FATAL_ERROR "${msg}")

endfunction()

macro (show_missing_lcov_error)
   message(FATAL_ERROR "TEST_GCOV/KERNEL_GCOV set but no lcov in toolchain. "
                       "Run ${BTC_SCRIPT} -s build_lcov first.")
endmacro()

macro (no_googletest_lib_fake_error_target)

   add_custom_target(

      gtests

      COMMAND echo
      COMMAND echo "==== ERROR: No googletest in toolchain ===="
      COMMAND echo
      COMMAND echo "Instructions:"
      COMMAND echo "  - Run ${BTC_SCRIPT} -s ${GTEST_BTC_COMMAND}"
      COMMAND echo "  - rm -rf ${CMAKE_BINARY_DIR}"
      COMMAND echo "  - Run this command again"
      COMMAND echo
   )

endmacro()
