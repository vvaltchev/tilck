cmake_minimum_required(VERSION 3.2)


if (${ARCH} STREQUAL "i386")

   set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-gcc)
   set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-gcc)
   set(OBJCOPY ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-objcopy)

else()

   message(FATAL_ERROR "Architecture '${ARCH}' not supported.")

endif()

file(GLOB EARLY_STAGES_SOURCES "boot.S")

set(

   BOOT_COMPILE_OPTS

   -Ttext 0x0
   -Wl,--oformat=binary
   -ffreestanding
   -nostdlib
   -nostdinc
   -I${CMAKE_SOURCE_DIR}/include
   -I${CMAKE_SOURCE_DIR}/include/system_headers
)

add_custom_command(
   OUTPUT
      early_boot.bin
   DEPENDS
      ${EARLY_STAGES_SOURCES}
   COMMAND
      ${CMAKE_C_COMPILER} -o early_boot.bin ${BOOT_COMPILE_OPTS} ${EARLY_STAGES_SOURCES}
   COMMENT
      "Building the early stages of the BIOS bootloader"
)

file(
   GLOB STAGE3_SOURCES

   "stage3_*.S"
   "*.c"
   "${CMAKE_SOURCE_DIR}/common/*.c"
)


# Remove -rdynamic
SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)

set(
   FLAGS_LIST

   -D__EXOS_BOOTLOADER__

   -g
   -mno-red-zone
   -ffreestanding
   -nostdinc
   -fno-builtin
   -fno-omit-frame-pointer

   #-O0
   #-fno-inline-functions

   -O2

   -Wall
   -Wextra
   -Wno-unused-function
   -Wno-unused-parameter
   -Werror

   -fno-pic
   -fno-asynchronous-unwind-tables
   -fno-zero-initialized-in-bss
   -fno-exceptions
   -fno-stack-protector

   # Allow easier disassembly debugging
   -mpush-args
   -mno-accumulate-outgoing-args
   -mno-stack-arg-probe
)
JOIN("${FLAGS_LIST}" ${SPACE} FLAGS)

set(
   LINK_FLAGS_LIST

   -nostdlib
   -L${CMAKE_SOURCE_DIR}/bootloader
   -Wl,--strip-all
   -Wl,--strip-debug
   -Wl,--script=${CMAKE_SOURCE_DIR}/bootloader/stage3_script.ld
)
JOIN("${LINK_FLAGS_LIST}" ${SPACE} LINK_FLAGS)

set(
   ELF_LINK_FLAGS_LIST

   -nostdlib
   -L${CMAKE_SOURCE_DIR}/bootloader
   -Wl,--script=${CMAKE_SOURCE_DIR}/bootloader/elf_stage3_script.ld
)
JOIN("${ELF_LINK_FLAGS_LIST}" ${SPACE} ELF_LINK_FLAGS)


add_executable(stage3 ${STAGE3_SOURCES})
add_executable(elf_stage3 EXCLUDE_FROM_ALL ${STAGE3_SOURCES})

include_directories(
   ${CMAKE_SOURCE_DIR}/include
   ${CMAKE_SOURCE_DIR}/include/system_headers
)

set_target_properties(

   stage3

   PROPERTIES
      COMPILE_FLAGS ${FLAGS}
      LINK_FLAGS ${LINK_FLAGS}
)

set_target_properties(

   elf_stage3

   PROPERTIES
      COMPILE_FLAGS ${FLAGS}
      LINK_FLAGS ${ELF_LINK_FLAGS}
)


# -lgcc is necessary for things like 64 bit integers in 32 bit mode.
target_link_libraries(stage3 gcc)
target_link_libraries(elf_stage3 gcc)


add_custom_command(
   OUTPUT
      ${BOOTLOADER_BIN_FILE}
   COMMAND
      cp early_boot.bin ${BOOTLOADER_BIN_FILE}
   COMMAND
      dd status=none conv=notrunc oflag=append if=stage3 of=${BOOTLOADER_BIN_FILE}
   DEPENDS
      early_boot.bin stage3
   COMMENT
      "Linking the BIOS bootloader"
)

add_custom_target(

   bootloader ALL

   DEPENDS
      ${BOOTLOADER_BIN_FILE}
)
