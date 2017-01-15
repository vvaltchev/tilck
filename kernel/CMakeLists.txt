cmake_minimum_required(VERSION 3.2)

set(KERNEL_LOAD_VADDR 0xC0100000)

set(GENERAL_KERNEL_FLAGS_LIST
   -mno-red-zone
   -ffreestanding
   -nostdinc
   -fno-builtin
   -fno-asynchronous-unwind-tables
   -fno-zero-initialized-in-bss
   -fno-exceptions
)

JOIN("${GENERAL_KERNEL_FLAGS_LIST}" "${SPACE}" GENERAL_KERNEL_FLAGS)

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${GENERAL_DEFS} ${GENERAL_CFLAGS} ${ARCH_FLAGS} ${OPT_FLAGS} ${WARN_FLAGS} ${GENERAL_KERNEL_FLAGS}")

include_directories("include")

file(GLOB ARCH_SOURCES "arch/${ARCH}/*.asm" "arch/${ARCH}/*.c")
file(GLOB SOURCES "*.c")

set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> -f elf32 -o <OBJECT> <SOURCE>")

add_executable(elf_kernel "${ARCH_SOURCES}" "${SOURCES}")
set_target_properties(

   elf_kernel

   PROPERTIES
      COMPILE_FLAGS "${GENERAL_DEFS} ${GENERAL_CFLAGS} ${ARCH_FLAGS} ${OPT_FLAGS} ${WARN_FLAGS} ${GENERAL_KERNEL_FLAGS}"
      LINK_FLAGS "${ARCH_FLAGS} -Wl,-Ttext=${KERNEL_LOAD_VADDR} -nostdlib"
)

target_link_libraries(elf_kernel gcc)

add_custom_command(
    OUTPUT
        ${KERNEL_BIN_FILE}
    COMMAND
        ${CMAKE_SOURCE_DIR}/build_scripts/bss_checker.sh elf_kernel
    COMMAND
        objcopy -O binary -j .text -j .rodata -j .data elf_kernel ${KERNEL_BIN_FILE}
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/elf_kernel
    COMMENT
        "Preparing kernel's flat-binary"
)

add_custom_target(

    kernel ALL

    DEPENDS
        ${KERNEL_BIN_FILE}
)

add_library(kernel_noarch_static_for_test STATIC "${SOURCES}")

set_target_properties(

   kernel_noarch_static_for_test

   PROPERTIES
      COMPILE_FLAGS "${GENERAL_DEFS} -DKERNEL_TEST ${GENERAL_CFLAGS} ${OPT_FLAGS} ${WARN_FLAGS} ${GENERAL_KERNEL_FLAGS}"
)
