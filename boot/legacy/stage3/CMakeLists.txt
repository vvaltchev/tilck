# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

file(
   GLOB STAGE3_C_SOURCES

   "*.c"
   "${CMAKE_SOURCE_DIR}/common/*.c"
   "${CMAKE_SOURCE_DIR}/common/arch/generic_x86/*.c"
)

set(STAGE3_ASM_SOURCES "entry.S" "rm_calls.S")

configure_file(
   linker_script.ld
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
   -fno-exceptions
   -fno-stack-protector
   -fwrapv
   -static
   -Wvla

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

   elf_stage3_unstripped

   ${STAGE3_ASM_SOURCES}
   ${STAGE3_C_SOURCES}
)

set_target_properties(

   elf_stage3_unstripped

   PROPERTIES
      COMPILE_FLAGS ${STAGE3_BUILD_FLAGS}
      LINK_FLAGS ${STAGE3_LINK_FLAGS}
      LINK_DEPENDS ${STAGE3_SCRIPT}
)


# -lgcc is necessary for things like 64 bit integers in 32 bit mode.
target_link_libraries(elf_stage3_unstripped gcc)
