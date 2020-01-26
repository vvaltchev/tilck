# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

set(
   efi_bootloader_deps

   efi_ia32_bootloader
   ${EFI_ia32_FILE}
)

add_subdirectory(ia32)

if ("${HOST_ARCH}" STREQUAL "x86_64")

   add_subdirectory(x86_64)

   list(
      APPEND efi_bootloader_deps

      efi_x86_64_bootloader
      ${EFI_x86_64_FILE}
   )
endif()

add_custom_target(

   efi_bootloader

   DEPENDS
      ${efi_bootloader_deps}
)
