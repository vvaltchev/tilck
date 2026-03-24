# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

project(boot_legacy LANGUAGES C CXX ASM)

include(${TILCK_CONFIG_FILE})
set(GLOB_CONF_DEP CONFIGURE_DEPENDS)
include(${PROJ_ROOT}/other/cmake/utils.cmake)
include(${PROJ_ROOT}/other/cmake/compiler_flags.cmake)

set(ELFHACK32 ${BUILD_APPS}/elfhack32)

# Common directories, both for early_boot and for elf_stage3_unstripped
include_directories(
   ${BUILD_DIR}
   ${PROJ_ROOT}/include
   ${PROJ_ROOT}/include/system_headers
)

link_directories(
   ${PROJ_ROOT}/boot/legacy
)

set(EARLY_STAGES_SOURCES "boot.S" "boot_enable_a20.S")

add_executable(
   early_boot EXCLUDE_FROM_ALL

   ${EARLY_STAGES_SOURCES}
)

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
      ${CMAKE_STRIP} --strip-debug stage3/elf_stage3_unstripped -o elf_stage3
   COMMAND
      ${ELFHACK32} elf_stage3 --verify-flat-elf
   COMMAND
      ${ELFHACK32} elf_stage3 --check-entry-point ${BL_BASE_ADDR}
   DEPENDS
      elf_stage3_unstripped
)

add_custom_command(
   OUTPUT
      ${BOOTLOADER_BIN_FILE}
   COMMAND
      cp early_boot ${BOOTLOADER_BIN_FILE}
   COMMAND
      ${DD} ${dd_opts1} if=elf_stage3 of=${BOOTLOADER_BIN_FILE}
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
