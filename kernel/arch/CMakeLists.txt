cmake_minimum_required(VERSION 3.2)

if (${ARCH} STREQUAL "i386")

   set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-gcc)
   set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-gcc)
   set(OBJCOPY ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-objcopy)
   set(STRIP ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-strip)

else()

   set(OBJCOPY objcopy)
   set(STRIP strip)

endif()

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


add_custom_command(

   OUTPUT
      font.o
   COMMAND
      ${OBJCOPY} -O elf32-i386 -B i386 -I binary font.psf ${CMAKE_CURRENT_BINARY_DIR}/font.o
   WORKING_DIRECTORY
      ${CMAKE_SOURCE_DIR}/kernel/fb
   DEPENDS
      ${CMAKE_SOURCE_DIR}/kernel/fb/font.psf
   COMMENT
      "Copying the psf font file into an ELF object file"
)

add_custom_target(

    font

    DEPENDS
      font.o
)

set_target_properties(

   elf_kernel

   PROPERTIES
      COMPILE_FLAGS "${KERNEL_FLAGS} ${ACTUAL_KERNEL_ONLY_FLAGS}"
      LINK_FLAGS ${KERNEL_LINK_FLAGS}
)

add_dependencies(elf_kernel font)
target_link_libraries(elf_kernel ${CMAKE_CURRENT_BINARY_DIR}/font.o)



# -lgcc is necessary for things like 64 bit integers in 32 bit mode.
target_link_libraries(elf_kernel gcc)

add_custom_command(
   OUTPUT
      ${KERNEL_FILE}
   COMMAND
      ${STRIP} --strip-debug elf_kernel -o ${KERNEL_FILE}
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
   COMMAND
      ${BUILD_APPS}/elfhack ${KERNEL_FILE} --drop-last-section
   COMMAND
      ${BUILD_APPS}/elfhack ${KERNEL_FILE} --drop-last-section
   COMMAND
      ${BUILD_APPS}/elfhack ${KERNEL_FILE} --set-phdr-rwx-flags 2 r
   DEPENDS
      elfhack ${CMAKE_CURRENT_BINARY_DIR}/elf_kernel
   COMMENT
      "Preparing kernel's stripped binary"
)

add_custom_target(

    kernel ALL

    DEPENDS
        ${KERNEL_FILE}
)

