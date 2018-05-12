cmake_minimum_required(VERSION 3.2)

set(EFI_ARCH x86_64)

file(GLOB ASM_SOURCES "*.S")
set(SWITCHMODE_BIN "switchmode.bin")

set(
   SWITCHMODE_COMPILE_OPTS

   -Ttext 0xC000
   -Wl,--oformat=binary
   -ffreestanding
   -nostdlib
   -nostdinc
   -I${CMAKE_SOURCE_DIR}/include
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

include (../GenericBuild.cmake)


