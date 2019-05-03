# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

# Remove -rdynamic
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)

set_cross_compiler()

# Common directories, both for early_boot and for elf_stage3_unstripped
include_directories(
   ${CMAKE_SOURCE_DIR}/include
   ${CMAKE_SOURCE_DIR}/include/system_headers
)

link_directories(
   ${CMAKE_SOURCE_DIR}/boot/legacy
)

set(EARLY_STAGES_SOURCES "boot.S" "boot_enable_a20.S")

add_executable(early_boot ${EARLY_STAGES_SOURCES})

set_target_properties(

   early_boot

   PROPERTIES
      COMPILE_FLAGS "-ffreestanding -nostdinc"
      LINK_FLAGS "-nostdlib -Wl,--script=${EARLY_BOOT_SCRIPT}"
      LINK_DEPENDS ${EARLY_BOOT_SCRIPT}
)

set(dd_opts1 "status=none" "conv=notrunc" "oflag=append")


add_custom_command(
   OUTPUT
      elf_stage3
   COMMAND
      ${TOOL_STRIP} --strip-debug stage3/elf_stage3_unstripped -o elf_stage3
   COMMAND
      elfhack elf_stage3 --verify-flat-elf
   COMMAND
      elfhack elf_stage3 --check-entry-point ${BL_BASE_ADDR}
   DEPENDS
      elf_stage3_unstripped
)

add_custom_command(
   OUTPUT
      ${BOOTLOADER_BIN_FILE}
   COMMAND
      cp early_boot ${BOOTLOADER_BIN_FILE}
   COMMAND
      dd ${dd_opts1} if=elf_stage3 of=${BOOTLOADER_BIN_FILE}
   DEPENDS
      early_boot elf_stage3
   COMMENT
      "Linking the legacy BIOS bootloader"
)

add_custom_target(

   bootloader ALL

   DEPENDS
      ${BOOTLOADER_BIN_FILE}
)

add_subdirectory(stage3)
