# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

set(EFI_BUILD_BASE_FLAGS_LIST "")

list(
   APPEND EFI_BUILD_BASE_FLAGS_LIST

   -D__TILCK_EFI_BOOTLOADER__

   -fno-stack-protector
   -fshort-wchar

   -DEFI_DEBUG=0                # Because of -Wundef
   -DEFI_DEBUG_CLEAR_MEMORY=0   # Because of -Wundef

   ${GENERAL_DEFS_LIST}
   ${DBG_FLAGS_LIST}
   ${OPT_FLAGS_LIST}
   ${WARN_FLAGS_LIST}
   ${FREESTANDING_FLAGS_LIST}
   ${SAFER_BEHAVIOR_FLAGS_LIST}
)

JOIN("${EFI_BUILD_BASE_FLAGS_LIST}" ${SPACE} EFI_BUILD_BASE_FLAGS)

set(
   efi_bootloader_deps
)

if (${ARCH} STREQUAL "i386")

   add_subdirectory(ia32)

   list(
      APPEND efi_bootloader_deps

      efi_ia32_bootloader
      ${EFI_ia32_FILE}
   )

endif()

if (${HOST_ARCH} STREQUAL "x86_64")

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
