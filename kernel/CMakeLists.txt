cmake_minimum_required(VERSION 3.2)

set(KERNEL_LOAD_VADDR 0xC0100000)

set(
   GENERAL_KERNEL_FLAGS_LIST

   -mno-red-zone
   -ffreestanding
   -nostdinc
   -fno-builtin
   -fno-asynchronous-unwind-tables
   -fno-zero-initialized-in-bss
   -fno-exceptions

   # TODO: use this option only when kernel is NOT compiled for tests
   # -fno-pic

   # Allow easier disassembly debugging
   -mpush-args
   -mno-accumulate-outgoing-args
   -mno-stack-arg-probe
)

JOIN("${GENERAL_KERNEL_FLAGS_LIST}" ${SPACE} GENERAL_KERNEL_FLAGS)

include_directories("include")

file(GLOB ARCH_SOURCES "arch/${ARCH}/*.asm" "arch/${ARCH}/*.c")
file(GLOB SOURCES "*.c" "test_stuff/*.c")

set(KERNEL_FLAGS "${COMMON_FLAGS} ${GENERAL_KERNEL_FLAGS}")

add_executable(elf_kernel ${ARCH_SOURCES} ${SOURCES})
set_target_properties(

   elf_kernel

   PROPERTIES
      COMPILE_FLAGS "${ARCH_FLAGS} ${KERNEL_FLAGS}"
      LINK_FLAGS "${ARCH_FLAGS} -Wl,-Ttext=${KERNEL_LOAD_VADDR} -nostdlib"
)

# -lgcc is necessary for things like 64 bit integers in 32 bit mode.
target_link_libraries(elf_kernel gcc)

add_custom_command(
    OUTPUT
        ${KERNEL_BIN_FILE}
    COMMAND
        ${BUILD_SCRIPTS}/bss_checker.sh elf_kernel
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


# Creating a static library target for putting kernel's arch-independent code.

add_library(kernel_noarch_static_for_test STATIC EXCLUDE_FROM_ALL ${SOURCES})

set_target_properties(

   kernel_noarch_static_for_test

   PROPERTIES
      COMPILE_FLAGS "-DKERNEL_TEST ${KERNEL_FLAGS} ${GCOV_COMPILE_FLAGS}"
)
