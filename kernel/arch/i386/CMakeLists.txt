# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

# Remove -rdynamic
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)


# Options that will be used only when building the actual kernel, not when
# building kernel's code for unit testing.

set(
   ACTUAL_KERNEL_ONLY_FLAGS_LIST

   -fno-pic
   -fno-asynchronous-unwind-tables
   -fno-exceptions
   -fno-stack-protector

   # Disable the generation of any kind of FPU instructions
   -mno-80387
   -mno-mmx
   -mno-sse
   -mno-avx

   # Allow easier disassembly debugging
   # -mpush-args
   # -mno-accumulate-outgoing-args
   # -mno-stack-arg-probe
)

if (KERNEL_GCOV)
   list(APPEND ACTUAL_KERNEL_ONLY_FLAGS_LIST -fprofile-arcs -ftest-coverage)
endif()

JOIN("${ACTUAL_KERNEL_ONLY_FLAGS_LIST}" ${SPACE} ACTUAL_KERNEL_ONLY_FLAGS)

set(ARCH_START_FILE "start.S")

file(GLOB ARCH_SOURCES "*.S" "*.c")

file(
      GLOB
      ARCH_FAMILY_SOURCES

      "${CMAKE_SOURCE_DIR}/kernel/arch/${ARCH_FAMILY}/*.c"
      "${CMAKE_SOURCE_DIR}/kernel/arch/${ARCH_FAMILY}/*/*.c"
      "${CMAKE_SOURCE_DIR}/common/arch/${ARCH_FAMILY}/*.c"
)

file(GLOB SOURCES ${KERNEL_NOARCH_SOURCES_GLOB})

# Override CMake's default executable output directory
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

add_executable(

   tilck_unstripped

   ${ARCH_START_FILE} ${ARCH_SOURCES} ${ARCH_FAMILY_SOURCES} ${SOURCES}
)

set(
   KERNEL_LINK_FLAGS_LIST

   -nostdlib
   -static

   -Wl,--build-id=none
   -Wl,--script=${KERNEL_SCRIPT}
)

JOIN("${KERNEL_LINK_FLAGS_LIST}" ${SPACE} KERNEL_LINK_FLAGS)

set_target_properties(

   tilck_unstripped

   PROPERTIES
      COMPILE_FLAGS "${KERNEL_FLAGS} ${ACTUAL_KERNEL_ONLY_FLAGS}"
      LINK_FLAGS "${KERNEL_LINK_FLAGS}"
      LINK_DEPENDS ${KERNEL_SCRIPT}
)

add_dependencies(tilck_unstripped fonts)

if (MOD_fb)
target_link_libraries(tilck_unstripped ${CMAKE_CURRENT_BINARY_DIR}/font8x16.o)
target_link_libraries(tilck_unstripped ${CMAKE_CURRENT_BINARY_DIR}/font16x32.o)
endif()

# -lgcc is necessary for things like 64 bit integers in 32 bit mode.
target_link_libraries(tilck_unstripped gcc)

add_custom_command(
   OUTPUT
      ${KERNEL_FILE}
   COMMAND
      ${TOOL_STRIP} --strip-debug ${KERNEL_UNSTRIPPED} -o ${KERNEL_FILE}
   COMMAND
      elfhack ${KERNEL_FILE} --copy .symtab .Symtab
   COMMAND
      elfhack ${KERNEL_FILE} --copy .strtab .Strtab
   COMMAND
      elfhack ${KERNEL_FILE} --link .Symtab .Strtab
   COMMAND
      elfhack ${KERNEL_FILE} --rename .symtab osymtab
   COMMAND
      elfhack ${KERNEL_FILE} --rename .strtab ostrtab
   COMMAND
      elfhack ${KERNEL_FILE} --rename .Symtab .symtab
   COMMAND
      elfhack ${KERNEL_FILE} --rename .Strtab .strtab
   COMMAND
      elfhack ${KERNEL_FILE} --move-metadata
   COMMAND
      elfhack ${KERNEL_FILE} --drop-last-section
   COMMAND
      elfhack ${KERNEL_FILE} --drop-last-section
   COMMAND
      elfhack ${KERNEL_FILE} --set-phdr-rwx-flags 2 r
   DEPENDS
      elfhack tilck_unstripped
   COMMENT
      "Preparing kernel's stripped binary"
)

add_custom_target(

    kernel ALL

    DEPENDS
        ${KERNEL_FILE}
)

