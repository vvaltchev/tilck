cmake_minimum_required(VERSION 3.2)

file(GLOB C_SOURCES "*.c")
set(ASM_SOURCE "switchmode.asm")

set(GNUEFI_DIR "${CMAKE_SOURCE_DIR}/toolchain/gnu-efi")
set(EFI_MAIN_SO "efimain.so")
set(EFI_MAIN_OBJ "efimain.o")
set(SWITCHMODE_OBJ "switchmode.o")

add_custom_command(
   OUTPUT
      ${SWITCHMODE_OBJ}
   DEPENDS
      ${ASM_SOURCE}
   COMMAND
      nasm -f bin -o ${SWITCHMODE_OBJ} ${ASM_SOURCE}
   COMMENT
      "Building ${SWITCHMODE_OBJ}"
)

add_custom_command(
   OUTPUT
      ${EFI_MAIN_OBJ}
   DEPENDS
      ${C_SOURCES}
   COMMAND
      ${CC} -o ${EFI_MAIN_OBJ} ${C_SOURCES} -std=c99 -c -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -I${GNUEFI_DIR}/inc -I${GNUEFI_DIR}/inc/x86_64 -DEFI_FUNCTION_WRAPPER
   COMMENT
      "Building ${EFI_MAIN_OBJ}"
)

add_custom_command(
   OUTPUT
      ${EFI_MAIN_SO}
   DEPENDS
      ${CMAKE_CURRENT_BINARY_DIR}/${EFI_MAIN_OBJ}
   COMMAND
     ld -o ${EFI_MAIN_SO} ${EFI_MAIN_OBJ} ${GNUEFI_DIR}/x86_64/gnuefi/crt0-efi-x86_64.o -nostdlib -znocombreloc -T ${GNUEFI_DIR}/gnuefi/elf_x86_64_efi.lds -shared -Bsymbolic -L${GNUEFI_DIR}/x86_64/lib -L${GNUEFI_DIR}/x86_64/gnuefi -l:libgnuefi.a -l:libefi.a
   COMMENT
      "Linking ${EFI_MAIN_SO}"
)


add_custom_command(
   OUTPUT
      ${EFI_MAIN_FILE}
   COMMAND
      objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc --target=efi-app-x86_64 ${EFI_MAIN_SO} ${EFI_MAIN_FILE}
   DEPENDS
      ${CMAKE_CURRENT_BINARY_DIR}/${SWITCHMODE_OBJ} ${CMAKE_CURRENT_BINARY_DIR}/${EFI_MAIN_SO}
   COMMENT
      "Creating ${EFI_MAIN_FILE} from ${EFI_MAIN_SO}"
)

add_custom_target(

   efi_bootloader ALL

   DEPENDS
      ${EFI_MAIN_FILE}
)
