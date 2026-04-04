# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

project(efi_ia32 LANGUAGES C CXX ASM)

include(${TILCK_CONFIG_FILE})
set(GLOB_CONF_DEP CONFIGURE_DEPENDS)
include(${PROJ_ROOT}/other/cmake/utils.cmake)
include(${PROJ_ROOT}/other/cmake/errors.cmake)
include(${PROJ_ROOT}/other/cmake/compiler_flags.cmake)

set(EFI_ARCH ia32)
set(GNUEFI_DIR ${TCROOT_ARCH_DIR_PARENT}/i386/gnuefi/${VER_GNUEFI})
file(GLOB SOURCES ${GLOB_CONF_DEP} "${PROJ_ROOT}/boot/efi/*.c")

include(${PROJ_ROOT}/boot/efi/GenericBuild.cmake)
