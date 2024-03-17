# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

set(EFI_ARCH ia32)
set(GNUEFI_DIR ${TCROOT}/i386/gnu-efi)
file(GLOB SOURCES ${GLOB_CONF_DEP} "../*.c")

set_cross_compiler()
include (../GenericBuild.cmake)
