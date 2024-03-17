# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

smart_config_file(linker_script.ld ${STAGE3_SCRIPT})

file(
   GLOB STAGE3_C_SOURCES ${GLOB_CONF_DEP}

   "*.c"
   "${CMAKE_SOURCE_DIR}/boot/common/*.c"
   "${CMAKE_SOURCE_DIR}/common/*.c"
   "${CMAKE_SOURCE_DIR}/common/*.cpp"
   "${CMAKE_SOURCE_DIR}/common/arch/generic_x86/*.c"
)

set(STAGE3_ASM_SOURCES "entry.S" "rm_calls.S")
set(STAGE3_BUILD_FLAGS_LIST "")

list(
   APPEND STAGE3_BUILD_FLAGS_LIST

   -D__TILCK_BOOTLOADER__
   -static
   -fno-pic
   -mno-red-zone

   ${GENERAL_DEFS_LIST}
   ${DBG_FLAGS_LIST}
   ${OPT_FLAGS_LIST}
   ${WARN_FLAGS_LIST}
   ${FREESTANDING_FLAGS_LIST}
   ${SAFER_BEHAVIOR_FLAGS_LIST}
   ${LOWLEVEL_BINARIES_FLAGS_LIST}
   ${DISABLE_FPU_FLAGS_LIST}
)

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

   elf_stage3_unstripped EXCLUDE_FROM_ALL

   ${STAGE3_ASM_SOURCES}
   ${STAGE3_C_SOURCES}
)

add_dependencies(elf_stage3_unstripped kernel)

set_target_properties(

   elf_stage3_unstripped

   PROPERTIES
      COMPILE_FLAGS ${STAGE3_BUILD_FLAGS}
      LINK_FLAGS ${STAGE3_LINK_FLAGS}
      LINK_DEPENDS ${STAGE3_SCRIPT}
)


# -lgcc is necessary for things like 64 bit integers in 32 bit mode.
target_link_libraries(elf_stage3_unstripped gcc)
