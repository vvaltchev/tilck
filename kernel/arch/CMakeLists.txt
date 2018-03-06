cmake_minimum_required(VERSION 3.2)

if (${ARCH} STREQUAL "i386")

   set(GCC_TOOLCHAIN ${TCROOT}/x86-i686--glibc--stable/bin)
   set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-gcc)
   set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-gcc)
   set(OBJCOPY ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-objcopy)
   set(STRIP ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-strip)

else()

   set(OBJCOPY objcopy)
   set(STRIP strip)

endif()


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

file(GLOB ARCH_SOURCES "${ARCH}/*.S" "${ARCH}/*.c")
file(GLOB ARCH_FAMILY_SOURCES "${ARCH_FAMILY}/*.c")
file(GLOB SOURCES ${KERNEL_NOARCH_SOURCES_GLOB})

add_executable(

   elf_kernel

   ${ARCH_SOURCES} ${ARCH_FAMILY_SOURCES} ${SOURCES}
)

set(
   KERNEL_LINK_FLAGS_LIST

   -Wl,--script=${CMAKE_SOURCE_DIR}/kernel/arch/${ARCH}/linker_script.ld
   -nostdlib
)
JOIN("${KERNEL_LINK_FLAGS_LIST}" ${SPACE} KERNEL_LINK_FLAGS)

set_target_properties(

   elf_kernel

   PROPERTIES
      COMPILE_FLAGS "${KERNEL_FLAGS} ${ACTUAL_KERNEL_ONLY_FLAGS}"
      LINK_FLAGS ${KERNEL_LINK_FLAGS}
)

# Remove -rdynamic
SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)


# -lgcc is necessary for things like 64 bit integers in 32 bit mode.
target_link_libraries(elf_kernel gcc)

add_custom_command(
   OUTPUT
      ${KERNEL_FILE}
   COMMAND
      ${STRIP} --strip-debug elf_kernel -o ${KERNEL_FILE}
   COMMAND
      gcc ${BUILD_APPS}/elfhack.c -std=c99 -o ${BUILD_APPS}/elfhack
   COMMAND
      ${BUILD_APPS}/elfhack ${KERNEL_FILE} --copy .symtab .Symtab
   COMMAND
      ${BUILD_APPS}/elfhack ${KERNEL_FILE} --copy .strtab .Strtab
   COMMAND
      ${BUILD_APPS}/elfhack ${KERNEL_FILE} --link .Symtab .Strtab
   COMMAND
      ${BUILD_APPS}/elfhack ${KERNEL_FILE} --rename .symtab osymtab
   COMMAND
      ${BUILD_APPS}/elfhack ${KERNEL_FILE} --rename .strtab ostrtab
   COMMAND
      ${BUILD_APPS}/elfhack ${KERNEL_FILE} --rename .Symtab .symtab
   COMMAND
      ${BUILD_APPS}/elfhack ${KERNEL_FILE} --rename .Strtab .strtab
   COMMAND
      ${BUILD_APPS}/elfhack ${KERNEL_FILE} --move_metadata
   DEPENDS
      ${CMAKE_CURRENT_BINARY_DIR}/elf_kernel
   COMMENT
      "Preparing kernel's stripped binary"
)

add_custom_target(

    kernel ALL

    DEPENDS
        ${KERNEL_FILE}
)

