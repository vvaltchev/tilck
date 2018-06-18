cmake_minimum_required(VERSION 3.2)

# Remove -rdynamic
SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)


# Options that will be used only when building the actual kernel, not when
# building kernel's code for unit testing.

set(
   ACTUAL_KERNEL_ONLY_FLAGS_LIST

   -fno-pic
   -fno-asynchronous-unwind-tables
   -fno-exceptions
   -fno-stack-protector

   # Allow easier disassembly debugging
   -mpush-args
   -mno-accumulate-outgoing-args
   -mno-stack-arg-probe
)
JOIN("${ACTUAL_KERNEL_ONLY_FLAGS_LIST}" ${SPACE} ACTUAL_KERNEL_ONLY_FLAGS)

file(GLOB ARCH_SOURCES "*.S" "*.c")

file(
      GLOB
      ARCH_FAMILY_SOURCES

      "${CMAKE_SOURCE_DIR}/kernel/arch/${ARCH_FAMILY}/*.c"
      "${CMAKE_SOURCE_DIR}/common/arch/${ARCH_FAMILY}/*.c"
)

file(GLOB SOURCES ${KERNEL_NOARCH_SOURCES_GLOB})

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

add_executable(

   elf_kernel

   ${ARCH_SOURCES} ${ARCH_FAMILY_SOURCES} ${SOURCES}
)

set_target_properties(

   elf_kernel

   PROPERTIES
      COMPILE_FLAGS "${KERNEL_FLAGS} ${ACTUAL_KERNEL_ONLY_FLAGS}"
      LINK_FLAGS "-nostdlib -Wl,--script=${KERNEL_SCRIPT}"
      LINK_DEPENDS ${KERNEL_SCRIPT}
)

add_dependencies(elf_kernel fonts)
target_link_libraries(elf_kernel ${CMAKE_CURRENT_BINARY_DIR}/font8x16.o)
target_link_libraries(elf_kernel ${CMAKE_CURRENT_BINARY_DIR}/font16x32.o)


# -lgcc is necessary for things like 64 bit integers in 32 bit mode.
target_link_libraries(elf_kernel gcc)

add_custom_command(
   OUTPUT
      ${KERNEL_FILE}
   COMMAND
      ${STRIP} --strip-debug ${EXECUTABLE_OUTPUT_PATH}/elf_kernel -o ${KERNEL_FILE}
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
      elfhack ${KERNEL_FILE} --move_metadata
   COMMAND
      elfhack ${KERNEL_FILE} --drop-last-section
   COMMAND
      elfhack ${KERNEL_FILE} --drop-last-section
   COMMAND
      elfhack ${KERNEL_FILE} --set-phdr-rwx-flags 2 r
   DEPENDS
      elfhack elf_kernel
   COMMENT
      "Preparing kernel's stripped binary"
)

add_custom_target(

    kernel ALL

    DEPENDS
        ${KERNEL_FILE}
)

