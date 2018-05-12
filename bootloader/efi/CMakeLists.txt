cmake_minimum_required(VERSION 3.2)

set(GNUEFI_DIR "${CMAKE_SOURCE_DIR}/toolchain/gnu-efi")

set(EFI_ARCH x86_64)

add_subdirectory(x86_64)

add_custom_target(

   efi_bootloader ALL

   DEPENDS
      efi_x86_64_bootloader
)
