cmake_minimum_required(VERSION 3.2)

file(GLOB C_SOURCES "*.c")
file(GLOB ASM_SOURCES "*.S")

set(GNUEFI_DIR "${CMAKE_SOURCE_DIR}/toolchain/gnu-efi")
set(SWITCHMODE_BIN "switchmode.bin")

# Remove -rdynamic
SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)

set(EFI_ARCH x86_64)

set(
   SWITCHMODE_COMPILE_OPTS

   -Ttext 0xC000
   -Wl,--oformat=binary
   -ffreestanding
   -nostdlib
   -nostdinc
   -I${CMAKE_SOURCE_DIR}/include/common
)

add_custom_command(
   OUTPUT
      ${SWITCHMODE_BIN}
   DEPENDS
      ${ASM_SOURCES}
   COMMAND
      ${CMAKE_C_COMPILER} -o ${SWITCHMODE_BIN} ${SWITCHMODE_COMPILE_OPTS} ${ASM_SOURCES}
   COMMENT
      "Building switchmode.bin"
)

################################################################################

set(
   COMPILE_FLAGS_LIST

   -DEFI_FUNCTION_WRAPPER
   -DNO_EXOS_ASSERT
   -DNO_EXOS_STATIC_WRAPPER
   -std=c99
   -fno-stack-protector
   -fpic
   -fshort-wchar
   -mno-red-zone
   -nostdinc
   -I${CMAKE_SOURCE_DIR}/include
   -I${CMAKE_SOURCE_DIR}/include/system_headers
   -I${GNUEFI_DIR}/inc
   -I${GNUEFI_DIR}/inc/${EFI_ARCH}
)

set(
   LINK_FLAGS_LIST

   -T${GNUEFI_DIR}/gnuefi/elf_${EFI_ARCH}_efi.lds
   -nostdlib
   -Wl,-znocombreloc
   -Wl,-Bsymbolic
)

JOIN("${COMPILE_FLAGS_LIST}" ${SPACE} COMPILE_FLAGS)
JOIN("${LINK_FLAGS_LIST}" ${SPACE} LINK_FLAGS)

set(
   OBJCOPY_OPTS

   -j .text -j .sdata -j .data -j .dynamic
   -j .dynsym -j .rel -j .rela -j .reloc

   --target=efi-app-${EFI_ARCH}
)

add_library(

   efi_app
   SHARED

   ${C_SOURCES}
)

set_target_properties(

   efi_app

   PROPERTIES
      COMPILE_FLAGS ${COMPILE_FLAGS}
      LINK_FLAGS ${LINK_FLAGS}
)

target_link_libraries(efi_app ${GNUEFI_DIR}/${EFI_ARCH}/gnuefi/crt0-efi-${EFI_ARCH}.o)
target_link_libraries(efi_app ${GNUEFI_DIR}/${EFI_ARCH}/lib/libefi.a)
target_link_libraries(efi_app ${GNUEFI_DIR}/${EFI_ARCH}/gnuefi/libgnuefi.a)

add_custom_command(
   OUTPUT
      ${EFI_MAIN_FILE}
   COMMAND
      objcopy ${OBJCOPY_OPTS} libefi_app.so ${EFI_MAIN_FILE}
   DEPENDS
      ${SWITCHMODE_BIN} efi_app
   COMMENT
      "Creating BOOTX64.EFI from libefi_app.so"
)

add_custom_target(

   efi_bootloader ALL

   DEPENDS
      ${EFI_MAIN_FILE}
)
