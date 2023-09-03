# SPDX-License-Identifier: BSD-2-Clause

# Remove -rdynamic
SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)

file(GLOB COMMON_SOURCES ${GLOB_CONF_DEP}

   "${CMAKE_SOURCE_DIR}/boot/common/*.c"
   "${CMAKE_SOURCE_DIR}/common/*.c"
   "${CMAKE_SOURCE_DIR}/common/*.cpp"
)

add_library(

   efi_app_${EFI_ARCH}
   SHARED EXCLUDE_FROM_ALL

   ${SOURCES}
   ${COMMON_SOURCES}
)

set(
   COMPILE_FLAGS_LIST

   -maccumulate-outgoing-args  # necessary for MS_ABI with older GCC compilers
   -fPIC
)

set(
   LINK_FLAGS_LIST

   -nostdlib
   -Wl,--script=${GNUEFI_DIR}/gnuefi/elf_${EFI_ARCH}_efi.lds
   -Wl,-znocombreloc
   -Wl,-Bsymbolic
)

JOIN("${COMPILE_FLAGS_LIST}" ${SPACE} COMPILE_FLAGS)
JOIN("${LINK_FLAGS_LIST}" ${SPACE} LINK_FLAGS)

set_target_properties(

   efi_app_${EFI_ARCH}

   PROPERTIES
      COMPILE_FLAGS "${EFI_BUILD_BASE_FLAGS} ${COMPILE_FLAGS}"
      LINK_FLAGS ${LINK_FLAGS}
)

target_include_directories(

   efi_app_${EFI_ARCH}

   PRIVATE

   ${CMAKE_SOURCE_DIR}/include
   ${CMAKE_SOURCE_DIR}/include/system_headers
   ${GNUEFI_DIR}/inc
   ${GNUEFI_DIR}/inc/${EFI_ARCH}
)

target_compile_definitions(

   efi_app_${EFI_ARCH}

   PRIVATE

   STATIC_TILCK_ASM_STRING
   EFI_FUNCTION_WRAPPER
   NO_TILCK_ASSERT
   NO_TILCK_STATIC_WRAPPER
   GNU_EFI_USE_MS_ABI        # allows to call UEFI funcs without the wrapper
)

target_link_libraries(

   efi_app_${EFI_ARCH}

   ${GNUEFI_DIR}/${EFI_ARCH}/gnuefi/crt0-efi-${EFI_ARCH}.o
   ${GNUEFI_DIR}/${EFI_ARCH}/lib/libefi.a
   ${GNUEFI_DIR}/${EFI_ARCH}/gnuefi/libgnuefi.a
)

add_dependencies(efi_app_${EFI_ARCH} kernel)

set(
   OC_OPTS # objcopy options

   -j .text -j .sdata -j .data -j .dynamic
   -j .dynsym -j .rel -j .rela -j .reloc

   --target=efi-app-${EFI_ARCH}
)

set(EFI_APP_SO libefi_app_${EFI_ARCH}.so)

add_custom_command(
   OUTPUT
      ${EFI_${EFI_ARCH}_FILE}
   COMMAND
      ${CMAKE_OBJCOPY} ${OC_OPTS} ${EFI_APP_SO} ${EFI_${EFI_ARCH}_FILE}
   DEPENDS
      ${SWITCHMODE_BIN}
      efi_app_${EFI_ARCH}
   COMMENT
      "Creating the final EFI file for ${EFI_ARCH}"
)

unset(EFI_APP_SO)

add_custom_target(

   efi_${EFI_ARCH}_bootloader

   DEPENDS
      ${EFI_${EFI_ARCH}_FILE}
)

