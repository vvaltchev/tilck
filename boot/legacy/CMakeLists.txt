# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

# Remove -rdynamic
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)


if (${ARCH} STREQUAL "i386")

   set_cross_compiler()

else()

   # If we're compiling the legacy bootloader, ${ARCH} is either i386 or x86_64
   # but that doesn't really matter, because the bootloader itself will be
   # always 32-bit. Now, since ${ARCH} is x86_64, we cannot really use our
   # pre-built GCC toolchain because it has been configured to work only
   # for its target architecture (x86_64 in this case) and fails in a cumbersome
   # ways if we try to use it with -m32 etc. Better just stick with the system
   # GCC. Fortunately, for the bootloader we won't need anything libmusl etc.
   # so it _should_ always work.

   set(CMAKE_C_FLAGS "-m32 -march=i686")
   set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} ${KERNEL_CXX_FLAGS}")
   set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS}")

   set(TOOL_GCOV "${GCOV}")

endif()


# Common directories, both for early_boot and for elf_stage3_unstripped
include_directories(
   ${CMAKE_SOURCE_DIR}/include
   ${CMAKE_SOURCE_DIR}/include/system_headers
)

link_directories(
   ${CMAKE_SOURCE_DIR}/boot/legacy
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
      elfhack32 elf_stage3 --verify-flat-elf
   COMMAND
      elfhack32 elf_stage3 --check-entry-point ${BL_BASE_ADDR}
   DEPENDS
      elf_stage3_unstripped
      elfhack32
      ${CMAKE_BINARY_DIR}/scripts/build_apps/elfhack32
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

   bootloader

   DEPENDS
      ${BOOTLOADER_BIN_FILE}
)

add_subdirectory(stage3)
