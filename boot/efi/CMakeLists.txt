# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

set(GNUEFI_DIR ${TCROOT}/gnu-efi)

add_subdirectory(ia32)

if ("${HOST_ARCH}" STREQUAL "x86_64")
   add_subdirectory(x86_64)
endif()

add_custom_target(

   efi_bootloader ALL

   DEPENDS
      efi_ia32_bootloader
)


if ("${HOST_ARCH}" STREQUAL "x86_64")
   add_dependencies(efi_bootloader efi_x86_64_bootloader)
endif()
