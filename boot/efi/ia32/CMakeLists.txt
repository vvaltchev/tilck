# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

set(EFI_ARCH ia32)
set(GNUEFI_DIR ${TCROOT_ARCH_DIR_PARENT}/i386/gnu-efi/${VER_GNUEFI})
file(GLOB SOURCES ${GLOB_CONF_DEP} "../*.c")

set_cross_compiler()
include (../GenericBuild.cmake)
