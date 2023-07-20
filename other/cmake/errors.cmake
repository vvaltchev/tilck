# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

function (show_missing_use_syscc_error)

   set(msg "")
   string(CONCAT msg "In order to build Tilck on this host architecture "
                     "the env variables USE_SYSCC, CC and CXX *must be* set. "
                     "WARNING: this scenario is *not* supported in an official"
                     " way, even if it should work.")

   message(FATAL_ERROR "\n${msg}")

endfunction()

function (show_same_arch_build_warning)

   set(msg "")
   string(CONCAT msg "Building with HOST_ARCH == ARCH and USE_SYSCC=1 is not"
                     " supported in an official way")

   message(WARNING "\n${msg}")

endfunction()

function (show_no_musl_syscc_error)

   set(msg "")
   string(CONCAT msg "In order to build with USE_SYSCC=1 libmusl *must be* "
                     "in the toolchain. "
                     "Run: ${BTC_SCRIPT_REL} -s build_libmusl first.")

   message(FATAL_ERROR "\n${msg}")

endfunction()

function (show_wconv_warning)

   set(msg "")
   string(CONCAT msg "WCONV (-Wconversion) is supported ONLY when the "
                     "kernel is built with Clang: "
                     "(CC=clang and CMAKE_ARGS='-DKERNEL_SYSCC=1')")

   message(WARNING "${msg}")

endfunction()

function (show_clang_and_syscc_error)

   set(msg "")
   string(CONCAT msg "USE_SYSCC=1 is not supported with Clang. "
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
   message(FATAL_ERROR "TEST_GCOV/KERNEL_GCOV set but no lcov-${LCOV_VER} in "
                       "toolchain. Run ${BTC_SCRIPT_REL} -s build_lcov first.")
endmacro()

macro (no_googletest_lib_fake_error_target)

   add_custom_target(

      gtests

      COMMAND echo
      COMMAND echo "==== ERROR: No googletest in toolchain ===="
      COMMAND echo
      COMMAND echo "Instructions:"
      COMMAND echo "  - Run ${BTC_SCRIPT_REL} -s ${GTEST_BTC_COMMAND}"
      COMMAND echo "  - ./scripts/cmake_run ${CMAKE_BINARY_DIR}"
      COMMAND echo "  - Run this command again"
      COMMAND echo
   )

endmacro()

function (show_no_ms_abi_support_warning relPath)

   set(msg "")
   string(CONCAT msg "Unable to build `${relPath}`: "
                     "the system compiler does not support MS_ABI. "
                     "If you need to use EFI boot on x86_64 machines "
                     "please use a non-ancient GCC compiler.")

   message(WARNING "\n${msg}\n")

endfunction()

function (show_arch_gtests_require_same_target_family_error)

   set(msg "")
   string(CONCAT msg "Building the arch gtests requires the host to have "
                     "the same arch of the target, or be in the same family. "
                     "ARCH_FAMILY: ${ARCH_FAMILY} vs "
                     "HOST_ARCH_FAMILY: ${HOST_ARCH_FAMILY}")

   message(FATAL_ERROR "\n${msg}\n")

endfunction()
