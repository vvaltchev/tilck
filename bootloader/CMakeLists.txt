cmake_minimum_required(VERSION 3.2)

# Remove -rdynamic
SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)

if (${ARCH} STREQUAL "i386")

   set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-gcc)
   set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-gcc)
   set(OBJCOPY ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-objcopy)
   set(STRIP ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-strip)

else()

   message(FATAL_ERROR "Architecture '${ARCH}' not supported.")

endif()

# Common include directories, both for early_boot and for elf_stage3
include_directories(
   ${CMAKE_SOURCE_DIR}/include
   ${CMAKE_SOURCE_DIR}/include/system_headers
)

file(GLOB EARLY_STAGES_SOURCES "boot.S")

set(
   EARLY_BOOT_LINK_FLAGS_LIST

   -nostdlib
   -L${CMAKE_SOURCE_DIR}/bootloader
   -Wl,--script=early_boot_script.ld
)
JOIN("${EARLY_BOOT_LINK_FLAGS_LIST}" ${SPACE} EARLY_BOOT_LINK_FLAGS)

add_executable(early_boot ${EARLY_STAGES_SOURCES})

set_target_properties(

   early_boot

   PROPERTIES
      COMPILE_FLAGS "-ffreestanding -nostdinc"
      LINK_FLAGS ${EARLY_BOOT_LINK_FLAGS}
)


file(
   GLOB STAGE3_SOURCES

   "stage3_*.S"
   "*.c"
   "${CMAKE_SOURCE_DIR}/common/*.c"
)



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
   ELF_LINK_FLAGS_LIST

   -nostdlib
   -L${CMAKE_SOURCE_DIR}/bootloader
   -Wl,--script=elf_stage3_script.ld
)
JOIN("${ELF_LINK_FLAGS_LIST}" ${SPACE} ELF_LINK_FLAGS)

add_executable(elf_stage3 ${STAGE3_SOURCES})



set_target_properties(

   elf_stage3

   PROPERTIES
      COMPILE_FLAGS ${FLAGS}
      LINK_FLAGS ${ELF_LINK_FLAGS}
)


# -lgcc is necessary for things like 64 bit integers in 32 bit mode.
target_link_libraries(elf_stage3 gcc)

add_custom_command(
   OUTPUT
      elf_stage3_stripped
   COMMAND
      ${STRIP} --strip-debug elf_stage3 -o elf_stage3_stripped
   DEPENDS
      elf_stage3
)


add_custom_command(
   OUTPUT
      ${BOOTLOADER_BIN_FILE}
   COMMAND
      cp early_boot ${BOOTLOADER_BIN_FILE}
   COMMAND
      dd status=none conv=notrunc oflag=append if=elf_stage3 of=${BOOTLOADER_BIN_FILE}
   DEPENDS
      early_boot elf_stage3_stripped
   COMMENT
      "Linking the BIOS bootloader"
)

add_custom_target(

   bootloader ALL

   DEPENDS
      ${BOOTLOADER_BIN_FILE}
)
