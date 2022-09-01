# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

# Remove -rdynamic
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)

# Options that will be used only when building the actual kernel, not when
# building kernel's code for unit testing.

set(ACTUAL_KERNEL_ONLY_FLAGS_LIST "")

list(
   APPEND ACTUAL_KERNEL_ONLY_FLAGS_LIST

   -D__ACTUAL_TILCK_KERNEL_BINARY__

   -mcmodel=large
   ${LOWLEVEL_BINARIES_FLAGS_LIST}
   ${DISABLE_FPU_FLAGS_LIST}
)

if (KERNEL_GCOV)
   list(APPEND ACTUAL_KERNEL_ONLY_FLAGS_LIST -fprofile-arcs -ftest-coverage)
endif()

JOIN("${ACTUAL_KERNEL_ONLY_FLAGS_LIST}" ${SPACE} ACTUAL_KERNEL_ONLY_FLAGS)

set(ARCH_START_FILE "start.S")
set(ARCH_SOURCES_GLOB "*.S" "*.c")

file(GLOB ARCH_SOURCES ${GLOB_CONF_DEP} ${ARCH_SOURCES_GLOB})

file(
   GLOB
   ARCH_FAMILY_SOURCES

   "${CMAKE_SOURCE_DIR}/kernel/arch/${ARCH_FAMILY}/*.c"
   "${CMAKE_SOURCE_DIR}/kernel/arch/${ARCH_FAMILY}/*/*.c"
   "${CMAKE_SOURCE_DIR}/common/arch/${ARCH_FAMILY}/*.c"
)

# Override CMake's default executable output directory
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

add_executable(

   tilck_unstripped

   ${ARCH_START_FILE}
   ${ARCH_SOURCES}
   ${ARCH_FAMILY_SOURCES}
   ${KERNEL_NOARCH_SOURCES}
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

build_all_modules(tilck_unstripped)

# -lgcc is necessary for things like 64 bit integers in 32 bit mode.
target_link_libraries(tilck_unstripped gcc)

if (KERNEL_SYMBOLS)

   add_custom_command(
      OUTPUT
         ${KERNEL_FILE}
      COMMAND
         ${BUILD_SCRIPTS}/tilck_set_commit_hash ${ELFHACK} ${KERNEL_UNSTRIPPED}
      COMMAND
         ${CMAKE_STRIP} --strip-debug ${KERNEL_UNSTRIPPED} -o ${KERNEL_FILE}
      COMMAND
         elfhack64 ${KERNEL_FILE} --copy .symtab .Symtab
      COMMAND
         elfhack64 ${KERNEL_FILE} --copy .strtab .Strtab
      COMMAND
         elfhack64 ${KERNEL_FILE} --link .Symtab .Strtab
      COMMAND
         elfhack64 ${KERNEL_FILE} --rename .symtab osymtab
      COMMAND
         elfhack64 ${KERNEL_FILE} --rename .strtab ostrtab
      COMMAND
         elfhack64 ${KERNEL_FILE} --rename .Symtab .symtab
      COMMAND
         elfhack64 ${KERNEL_FILE} --rename .Strtab .strtab
      COMMAND
         elfhack64 ${KERNEL_FILE} --move-metadata
      COMMAND
         elfhack64 ${KERNEL_FILE} --drop-last-section
      COMMAND
         elfhack64 ${KERNEL_FILE} --drop-last-section
      COMMAND
         elfhack64 ${KERNEL_FILE} --set-phdr-rwx-flags 2 r
      DEPENDS
         tilck_unstripped
         elfhack64
         ${CMAKE_BINARY_DIR}/scripts/build_apps/elfhack64
      COMMENT
         "Preparing kernel's stripped binary"
   )

else()

   add_custom_command(
      OUTPUT
         ${KERNEL_FILE}
      COMMAND
         ${BUILD_SCRIPTS}/tilck_set_commit_hash ${ELFHACK} ${KERNEL_UNSTRIPPED}
      COMMAND
         ${CMAKE_STRIP} --strip-debug ${KERNEL_UNSTRIPPED} -o ${KERNEL_FILE}
      COMMAND
         elfhack64 ${KERNEL_FILE} --move-metadata
      DEPENDS
         elfhack64 tilck_unstripped
      COMMENT
         "Preparing kernel's stripped binary"
   )
endif()

add_custom_target(

    kernel ALL

    DEPENDS
        ${KERNEL_FILE}
)

