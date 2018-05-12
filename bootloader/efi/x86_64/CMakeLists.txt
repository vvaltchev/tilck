cmake_minimum_required(VERSION 3.2)

file(GLOB ASM_SOURCES "*.S")

set(SWITCHMODE_BIN "switchmode.bin")

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

include (../GenericBuild.cmake)

add_custom_command(
   OUTPUT
      ${EFI_MAIN_FILE}
   COMMAND
      objcopy ${OBJCOPY_OPTS} libefi_app.so ${EFI_MAIN_FILE}
   DEPENDS
      ${SWITCHMODE_BIN} efi_app
   COMMENT
      "Creating the final EFI file for ${EFI_ARCH}"
)

add_custom_target(

   efi_${EFI_ARCH}_bootloader

   DEPENDS
      ${EFI_MAIN_FILE}
)



