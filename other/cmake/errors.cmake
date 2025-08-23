# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

function (show_wconv_warning)

   set(msg "")
   string(CONCAT msg "WCONV (-Wconversion) is supported ONLY when the "
                     "kernel is built with Clang: "
                     "(CC=clang and CMAKE_ARGS='-DKERNEL_SYSCC=1')")

   message(WARNING "${msg}")

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

function (show_arch_gtests_arch_error)

   set(msg "")
   string(CONCAT msg "Building the arch gtests make sense only when the host "
                     "arch is x86_64 and the target arch is i386. \n"
                     "ARCH: ${ARCH}, HOST_ARCH: ${HOST_ARCH}")

   message(FATAL_ERROR "\n${msg}\n")

endfunction()
