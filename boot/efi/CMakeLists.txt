# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

set(GNUEFI_DIR ${TCROOT}/gnu-efi)

add_subdirectory(ia32)
add_subdirectory(x86_64)

add_custom_target(

   efi_bootloader ALL

   DEPENDS
      efi_ia32_bootloader efi_x86_64_bootloader
)
