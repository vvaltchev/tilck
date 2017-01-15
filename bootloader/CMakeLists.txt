cmake_minimum_required(VERSION 3.2)

file(GLOB SOURCES "*.asm")

add_custom_command(
   OUTPUT
      boot.o
   DEPENDS
      ${SOURCES}
   COMMAND
      nasm -f bin -o boot.o ${SOURCES}
   COMMENT
      "Building the bootloader"
)

# This is not an actual linking since 'boot.o' is already a flat binary file
# but, in the future there may be C code in the bootloader as well.

add_custom_command(
   OUTPUT
      ${BOOTLOADER_BIN_FILE}
   COMMAND
      cp boot.o ${BOOTLOADER_BIN_FILE}
   DEPENDS
      ${CMAKE_CURRENT_BINARY_DIR}/boot.o
   COMMENT
      "Linking the bootloader"
)

add_custom_target(

   bootloader ALL

   DEPENDS
      ${BOOTLOADER_BIN_FILE}
)
