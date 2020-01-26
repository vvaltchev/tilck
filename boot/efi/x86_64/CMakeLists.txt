# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

set(EFI_ARCH x86_64)
set(GNUEFI_DIR ${TCROOT}/x86_64/gnu-efi)

CHECK_C_COMPILER_FLAG(-maccumulate-outgoing-args ACC_OUTGOING_ARGS_SUPPORTED)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU" AND ACC_OUTGOING_ARGS_SUPPORTED)

   file(GLOB SOURCES "../*.c" "*.S")
   include (../GenericBuild.cmake)

else()

   file(RELATIVE_PATH relPath ${CMAKE_BINARY_DIR} ${EFI_${EFI_ARCH}_FILE})

   if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
      show_no_ms_abi_support_warning(${relPath})
   else()
      show_clang_not_supported_for_efi_warning(${relPath})
   endif()

   add_custom_command(
      OUTPUT
         ${EFI_${EFI_ARCH}_FILE}
      COMMAND
         touch ${EFI_${EFI_ARCH}_FILE}
   )

   add_custom_target(

      efi_${EFI_ARCH}_bootloader

      DEPENDS
         ${EFI_${EFI_ARCH}_FILE}
   )

endif()

