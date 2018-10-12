# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

set(EFI_ARCH x86_64)

CHECK_C_COMPILER_FLAG(-maccumulate-outgoing-args MS_ABI_SUPPORTED)

if (${MS_ABI_SUPPORTED})

   file(GLOB SOURCES "../*.c" "*.S")
   include (../GenericBuild.cmake)

else()

   string(CONCAT msg "Unable to build ${EFI_${EFI_ARCH}_FILE}: "
                     "the system compiler does not support MS_ABI. "
                     "If you really need to use EFI boot on x86_64 machines "
                     "please use GCC as C compiler.")

   message(WARNING "${msg}")

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

