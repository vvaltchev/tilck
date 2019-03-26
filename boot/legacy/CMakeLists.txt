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
   ${CMAKE_SOURCE_DIR}/boot/legacy
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

set(STAGE3_ASM_SOURCES "stage3_entry.S" "stage3_rm_calls.S")

#### COMMENTED BLOCK: See the comments in elf_stage3_script.ld.

#set(tmp "${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}")
#set(STAGE3_STARTUP_OBJ_FILE "${tmp}/elf_stage3.dir/stage3_entry.S.o")
#unset(tmp)

#### END COMMENTED BLOCK


configure_file(
   ${CMAKE_SOURCE_DIR}/boot/legacy/elf_stage3_script.ld
   ${STAGE3_SCRIPT}
   @ONLY
)

set(
   STAGE3_BUILD_FLAGS_LIST

   -D__TILCK_BOOTLOADER__

   -ffreestanding
   -fno-builtin
   -fno-omit-frame-pointer
   -fno-pic
   -fno-asynchronous-unwind-tables
   -fno-zero-initialized-in-bss
   -fno-exceptions
   -fno-stack-protector
   -static

   # Disable the generation of any kind of FPU instructions
   -mno-80387
   -mno-fp-ret-in-387
   -mno-mmx
   -mno-sse
   -mno-avx

   # Allow easier disassembly debugging
   # -mpush-args
   # -mno-accumulate-outgoing-args
   # -mno-stack-arg-probe
)

list(APPEND STAGE3_BUILD_FLAGS_LIST ${GENERAL_DEFS_LIST})
list(APPEND STAGE3_BUILD_FLAGS_LIST ${OTHER_CFLAGS_LIST})
list(APPEND STAGE3_BUILD_FLAGS_LIST ${OPT_FLAGS_LIST})
list(APPEND STAGE3_BUILD_FLAGS_LIST ${WARN_FLAGS_LIST})

JOIN("${STAGE3_BUILD_FLAGS_LIST}" ${SPACE} STAGE3_BUILD_FLAGS)

set(
   STAGE3_LINK_FLAGS_LIST

   -nostdlib
   -static
   -Wl,--build-id=none
   -Wl,--script=${STAGE3_SCRIPT}
)
JOIN("${STAGE3_LINK_FLAGS_LIST}" ${SPACE} STAGE3_LINK_FLAGS)

add_executable(

   elf_stage3

   ${STAGE3_ASM_SOURCES}
   ${STAGE3_C_SOURCES}
)

set_target_properties(

   elf_stage3

   PROPERTIES
      COMPILE_FLAGS ${STAGE3_BUILD_FLAGS}
      LINK_FLAGS ${STAGE3_LINK_FLAGS}
      LINK_DEPENDS ${STAGE3_SCRIPT}
)


# -lgcc is necessary for things like 64 bit integers in 32 bit mode.
target_link_libraries(elf_stage3 gcc)

add_custom_command(
   OUTPUT
      elf_stage3_stripped
   COMMAND
      ${TOOL_STRIP} --strip-debug elf_stage3 -o elf_stage3_stripped
   COMMAND
      elfhack elf_stage3_stripped --verify-flat-elf
   COMMAND
      elfhack elf_stage3_stripped --check-entry-point ${BL_BASE_ADDR}
   DEPENDS
      elf_stage3
)

set(dd_opts1 "status=none" "conv=notrunc" "oflag=append")

add_custom_command(
   OUTPUT
      ${BOOTLOADER_BIN_FILE}
   COMMAND
      cp early_boot ${BOOTLOADER_BIN_FILE}
   COMMAND
      dd ${dd_opts1} if=elf_stage3_stripped of=${BOOTLOADER_BIN_FILE}
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
