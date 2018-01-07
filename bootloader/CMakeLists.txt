cmake_minimum_required(VERSION 3.2)

if (${ARCH} STREQUAL "i386")

   set(GCC_TOOLCHAIN ${TCROOT}/x86-i686--glibc--stable/bin)
   set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-gcc)
   set(OBJCOPY ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-objcopy)

else()

   set(OBJCOPY objcopy)

endif()




file(GLOB EARLY_STAGES_SOURCES "boot.asm")

add_custom_command(
   OUTPUT
      boot.o
   DEPENDS
      ${EARLY_STAGES_SOURCES}
   COMMAND
      ${CMAKE_ASM_NASM_COMPILER} -f bin -o boot.o ${EARLY_STAGES_SOURCES}
   COMMENT
      "Building the early stages of the BIOS bootloader"
)

# file(GLOB STAGE3_SOURCES "*.c")

# add_executable(elf_stage3 ${STAGE3_SOURCES})

# add_custom_command(
#     OUTPUT
#         stage3
#     COMMAND
#         ${BUILD_SCRIPTS}/bss_checker elf_stage3
#     COMMAND
#         ${OBJCOPY} -O binary -j .text -j .rodata -j .data elf_stage3 stage3
#     DEPENDS
#         ${CMAKE_CURRENT_BINARY_DIR}/elf_stage3
#     COMMENT
#         "Preparing bootloaders's flat-binary"
# )

add_custom_command(
   OUTPUT
      ${BOOTLOADER_BIN_FILE}
   COMMAND
      cp boot.o ${BOOTLOADER_BIN_FILE}
#   COMMAND
#      dd status=none conv=notrunc if=${CMAKE_CURRENT_BINARY_DIR}/stage3 of=${BOOTLOADER_BIN_FILE}
   DEPENDS
      ${CMAKE_CURRENT_BINARY_DIR}/boot.o
   COMMENT
      "Linking the BIOS bootloader"
)

add_custom_target(

   bootloader ALL

   DEPENDS
      ${BOOTLOADER_BIN_FILE}
)
