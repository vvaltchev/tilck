cmake_minimum_required(VERSION 3.2)

set(KERNEL_LOAD_VADDR 0xC0100000)

if (${ARCH} STREQUAL "i386")

   set(GCC_TOOLCHAIN ${TCROOT}/x86-i686--glibc--stable/bin)
   set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-gcc)
   set(OBJCOPY ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-objcopy)

else()

   set(OBJCOPY objcopy)

endif()


# Options that will be used only when building the actual kernel, not when
# building kernel's code for unit testing.

set(
   ACTUAL_KERNEL_ONLY_FLAGS_LIST

   -fno-pic
   -fno-asynchronous-unwind-tables
   -fno-zero-initialized-in-bss
   -fno-exceptions

   # Allow easier disassembly debugging
   -mpush-args
   -mno-accumulate-outgoing-args
   -mno-stack-arg-probe
)
JOIN("${ACTUAL_KERNEL_ONLY_FLAGS_LIST}" ${SPACE} ACTUAL_KERNEL_ONLY_FLAGS)


file(GLOB ARCH_SOURCES "${ARCH}/*.asm" "${ARCH}/*.c")
file(GLOB SOURCES "../*.c" "../fs/*.c" "../test_stuff/*.c")


add_executable(elf_kernel ${ARCH_SOURCES} ${SOURCES})

set_target_properties(

   elf_kernel

   PROPERTIES
      COMPILE_FLAGS "${ARCH_FLAGS} ${KERNEL_FLAGS} ${ACTUAL_KERNEL_ONLY_FLAGS}"
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
        ${OBJCOPY} -O binary -j .text -j .rodata -j .data elf_kernel ${KERNEL_BIN_FILE}
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

