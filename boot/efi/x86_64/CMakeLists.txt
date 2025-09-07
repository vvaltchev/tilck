# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

set(EFI_ARCH x86_64)
set(GNUEFI_DIR ${TCROOT_ARCH_DIR_PARENT}/x86_64/gnuefi/${VER_GNUEFI})
file(GLOB SOURCES ${GLOB_CONF_DEP} "../*.c" "*.S")

set(NAME_PFX_x86_64       gcc_${GCC_TC_VER_}_x86_64)
set(TC_ROOT_x86_64        ${TCROOT_BA}/${NAME_PFX_x86_64}_musl)
set(TOOLCHAIN_x86_64      ${TC_ROOT_x86_64}/bin)

set_cross_compiler_internal(${TOOLCHAIN_x86_64} x86_64)
include (../GenericBuild.cmake)

