cmake_minimum_required(VERSION 3.2)

file(GLOB C_SOURCES "*.c")
file(GLOB ASM_SOURCES "*.asm")

set(GNUEFI_DIR "${CMAKE_SOURCE_DIR}/toolchain/gnu-efi")
set(EFI_MAIN_SO "efimain.so")
set(EFI_MAIN_OBJ "efimain.o")
set(SWITCHMODE_BIN ${CMAKE_BINARY_DIR}/switchmode.bin)

set(
   EFI_CFLAGS_LIST

   -std=c99 -c -fno-stack-protector -fpic -fshort-wchar -mno-red-zone
   -DEFI_FUNCTION_WRAPPER
   -nostdinc
   -DNO_EXOS_ASSERT
   -DNO_EXOS_STATIC_WRAPPER
   -I${CMAKE_SOURCE_DIR}/common/include
   -I${CMAKE_SOURCE_DIR}/common/system_headers
   -I${GNUEFI_DIR}/inc
   -I${GNUEFI_DIR}/inc/x86_64
)

set(
   EFI_SO_LINK_OPTS

   ${GNUEFI_DIR}/x86_64/gnuefi/crt0-efi-x86_64.o -nostdlib -znocombreloc
   -T ${GNUEFI_DIR}/gnuefi/elf_x86_64_efi.lds -shared -Bsymbolic
   -L${GNUEFI_DIR}/x86_64/lib -L${GNUEFI_DIR}/x86_64/gnuefi
   -l:libgnuefi.a -l:libefi.a
)

set(
   OBJCOPY_OPTS

   -j .text -j .sdata -j .data -j .dynamic
   -j .dynsym -j .rel -j .rela -j .reloc

   --target=efi-app-x86_64
)

add_custom_command(
   OUTPUT
      ${SWITCHMODE_BIN}
   DEPENDS
      ${ASM_SOURCES}
   COMMAND
      ${CMAKE_ASM_NASM_COMPILER} -f bin -o ${SWITCHMODE_BIN} ${ASM_SOURCES}
   COMMENT
      "Building switchmode.bin"
)

add_custom_command(
   OUTPUT
      ${EFI_MAIN_OBJ}
   DEPENDS
      ${C_SOURCES}
   COMMAND
      ${CMAKE_C_COMPILER} -o ${EFI_MAIN_OBJ} ${C_SOURCES} ${EFI_CFLAGS_LIST}
   COMMENT
      "Building ${EFI_MAIN_OBJ}"
)

add_custom_command(
   OUTPUT
      ${EFI_MAIN_SO}
   DEPENDS
      ${CMAKE_CURRENT_BINARY_DIR}/${EFI_MAIN_OBJ}
   COMMAND
     ld -o ${EFI_MAIN_SO} ${EFI_MAIN_OBJ} ${EFI_SO_LINK_OPTS}
   COMMENT
      "Linking ${EFI_MAIN_SO}"
)


add_custom_command(
   OUTPUT
      ${EFI_MAIN_FILE}
   COMMAND
      objcopy ${OBJCOPY_OPTS} ${EFI_MAIN_SO} ${EFI_MAIN_FILE}
   DEPENDS
      ${SWITCHMODE_BIN} ${CMAKE_CURRENT_BINARY_DIR}/${EFI_MAIN_SO}
   COMMENT
      "Creating BOOTX64.EFI from ${EFI_MAIN_SO}"
)

add_custom_target(

   efi_bootloader ALL

   DEPENDS
      ${EFI_MAIN_FILE}
)
