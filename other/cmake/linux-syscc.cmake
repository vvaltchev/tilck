# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

#
# KERNEL_SYSCC toolchain file.
#
# Use the host's C and C++ compilers for kernel C/CXX sources but
# everything else (assembler, objcopy, strip, ar, ranlib) from
# Tilck's cross-toolchain. The mix supports the static-analysis
# build paths (clang_small_offt / clang_wconv / clang_tc_isystem
# generators) on darwin hosts -- darwin's CommandLineTools doesn't
# ship objcopy at all and clang's integrated assembler can't
# handle Tilck's GAS intel-syntax .S files. Setting
# CMAKE_SYSTEM_NAME=Linux also tells cmake we're cross-compiling
# so it stops auto-adding darwin-host flags (`-arch <host>`,
# `-isysroot <SDK>`) to all targets.
#
# Required parameters from the parent project (passed via -D):
#
#   ARCH                  one of: i386, x86_64, riscv64
#   GCC_TOOLCHAIN_BIN     path to the cross-toolchain's bin/ dir
#   CMAKE_C_COMPILER      host C compiler (cmake-standard)
#   CMAKE_CXX_COMPILER    host C++ compiler (cmake-standard)
#

# Forward ARCH / GCC_TOOLCHAIN_BIN to try_compile sub-projects
# (e.g. CMakeDetermineCompilerABI), which by default only carry a
# curated subset of cache variables across to their nested cmake
# invocations. Without this, the toolchain file re-evaluates with
# both undefined and the binutils setup below fails.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
   ARCH GCC_TOOLCHAIN_BIN)

if (ARCH STREQUAL "i386")
   set(_arch_tc "i686")
elseif (ARCH STREQUAL "x86_64" OR ARCH STREQUAL "riscv64")
   set(_arch_tc "${ARCH}")
else()
   message(FATAL_ERROR "linux-syscc.cmake: unsupported ARCH '${ARCH}'")
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR ${_arch_tc})

# Drop -rdynamic on shared-lib links (matches the pattern in
# i386-linux-gcc.cmake and other per-arch toolchain files).
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)

# Assembler: cross-toolchain gcc. Tilck's .S files use GAS
# intel-syntax, which clang's integrated assembler doesn't fully
# support; route .S through the cross-toolchain's gcc instead.
set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN_BIN}/${_arch_tc}-linux-gcc)

# Binutils helpers: cross-toolchain. darwin's CommandLineTools
# doesn't ship objcopy; a Linux host's /usr/bin/objcopy may
# target the wrong arch by default. The cross-toolchain's
# binutils are correct for our ELF<bits>-<arch> output
# everywhere.
set(CMAKE_OBJCOPY ${GCC_TOOLCHAIN_BIN}/${_arch_tc}-linux-objcopy)
set(CMAKE_STRIP   ${GCC_TOOLCHAIN_BIN}/${_arch_tc}-linux-strip)
set(CMAKE_AR      ${GCC_TOOLCHAIN_BIN}/${_arch_tc}-linux-ar)
set(CMAKE_RANLIB  ${GCC_TOOLCHAIN_BIN}/${_arch_tc}-linux-ranlib)
