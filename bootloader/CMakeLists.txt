# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

# Remove -rdynamic
SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)

set_cross_compiler()

# Common directories, both for early_boot and for elf_stage3
include_directories(
   ${CMAKE_SOURCE_DIR}/include
   ${CMAKE_SOURCE_DIR}/include/system_headers
)

link_directories(
   ${CMAKE_SOURCE_DIR}/bootloader
)

file(GLOB EARLY_STAGES_SOURCES "boot.S" "boot_enable_a20.S")

add_executable(early_boot ${EARLY_STAGES_SOURCES})

set_target_properties(

   early_boot

   PROPERTIES
      COMPILE_FLAGS "-ffreestanding -nostdinc"
      LINK_FLAGS "-nostdlib -Wl,--script=${EARLY_BOOT_SCRIPT}"
      LINK_DEPENDS ${EARLY_BOOT_SCRIPT}
)


file(
   GLOB STAGE3_C_SOURCES

   "*.c"
   "${CMAKE_SOURCE_DIR}/common/*.c"
   "${CMAKE_SOURCE_DIR}/common/arch/generic_x86/*.c"
)

set(STAGE3_ASM_FILES "stage3_entry.S" "stage3_rm_calls.S")

#### COMMENTED BLOCK: See the comments in elf_stage3_script.ld.

#set(tmp "${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}")
#set(STAGE3_STARTUP_OBJ_FILE "${tmp}/elf_stage3.dir/stage3_entry.S.o")
#unset(tmp)

#### END COMMENTED BLOCK


configure_file(
   ${CMAKE_SOURCE_DIR}/bootloader/elf_stage3_script.ld
   ${STAGE3_SCRIPT}
   @ONLY
)

set(
   FLAGS_LIST

   -D__TILCK_BOOTLOADER__

   -g
   -mno-red-zone
   -ffreestanding
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

add_executable(

   elf_stage3

   ${STAGE3_ASM_FILES}
   ${STAGE3_C_SOURCES}
)

set_target_properties(

   elf_stage3

   PROPERTIES
      COMPILE_FLAGS ${FLAGS}
      LINK_FLAGS "-nostdlib -Wl,--script=${STAGE3_SCRIPT}"
      LINK_DEPENDS ${STAGE3_SCRIPT}
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
      dd status=none conv=notrunc oflag=append if=elf_stage3_stripped of=${BOOTLOADER_BIN_FILE}
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
