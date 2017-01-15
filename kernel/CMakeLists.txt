cmake_minimum_required(VERSION 3.2)

set(CMAKE_C_COMPILER "gcc")

# Remove cmake's default options
SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")


set(GENERAL_KERNEL_FLAGS_LIST
   -g
   -std=c11
   -mno-red-zone
   -fvisibility=default
   -ffreestanding
   -nostdinc
   -fno-builtin
   -fno-asynchronous-unwind-tables
   -fno-zero-initialized-in-bss
   -fno-exceptions
)

JOIN("${GENERAL_KERNEL_FLAGS_LIST}" "${SPACE}" GENERAL_KERNEL_FLAGS)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${GENERAL_DEFS} ${OPT_FLAGS} ${WARN_FLAGS} ${GENERAL_KERNEL_FLAGS}")

include_directories("include")

file(GLOB SOURCES "arch/${ARCH}/*.asm" "arch/${ARCH}/*.c" "*.c")


set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> -f elf32 -o <OBJECT> <SOURCE>")

set_source_files_properties(${SOURCES} PROPERTIES COMPILE_FLAGS ${ARCH_FLAGS})

add_executable(elf_kernel "${SOURCES}")
set_target_properties(elf_kernel PROPERTIES LINK_FLAGS "${ARCH_FLAGS} -Wl,-Ttext=0xC0100000 -nostdlib")
target_link_libraries(elf_kernel gcc)

add_custom_command(
    OUTPUT
        ${CMAKE_BINARY_DIR}/kernel.bin
    COMMAND
        ${CMAKE_SOURCE_DIR}/build_scripts/bss_checker.sh elf_kernel
    COMMAND
        objcopy -O binary -j .text -j .rodata -j .data elf_kernel ${CMAKE_BINARY_DIR}/kernel.bin
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/elf_kernel
    COMMENT
        "Preparing kernel's flat-binary"
)

add_custom_target(

    kernel ALL

    DEPENDS
        ${CMAKE_BINARY_DIR}/kernel.bin
)
