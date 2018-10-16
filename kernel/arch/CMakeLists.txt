# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-gcc)

if (${KERNEL_USE_SYSTEM_CC})
   if (${ARCH} STREQUAL "i386")
      set(CMAKE_C_FLAGS "-m32 -march=i686")
      set(OBJCOPY "${SYS_OBJCOPY}")
      set(STRIP "${SYS_STRIP}")
   else()
      message(FATAL_ERROR "Architecture '${ARCH}' not supported.")
   endif()
else()
   set_cross_compiler()
endif()

# B2O = Binary to Object file [options]
list(APPEND B2O -O ${ARCH_ELF_NAME} -B ${ARCH} -I binary)

set(FONT8x16 ${CMAKE_CURRENT_BINARY_DIR}/${ARCH}/font8x16.o)
set(FONT16x32 ${CMAKE_CURRENT_BINARY_DIR}/${ARCH}/font16x32.o)

add_custom_command(

   OUTPUT
      ${FONT8x16}
   COMMAND
      ${OBJCOPY} ${B2O} font8x16.psf ${FONT8x16}
   WORKING_DIRECTORY
      ${CMAKE_SOURCE_DIR}/kernel/fb
   DEPENDS
      ${CMAKE_SOURCE_DIR}/kernel/fb/font8x16.psf
   COMMENT
      "Copying the psf font (8x16) file into an ELF object file"
)

add_custom_command(

   OUTPUT
      ${FONT16x32}
   COMMAND
      ${OBJCOPY} ${B2O} font16x32.psf ${FONT16x32}
   WORKING_DIRECTORY
      ${CMAKE_SOURCE_DIR}/kernel/fb
   DEPENDS
      ${CMAKE_SOURCE_DIR}/kernel/fb/font16x32.psf
   COMMENT
      "Copying the psf font (16x32) file into an ELF object file"
)

add_custom_target(

    fonts

    DEPENDS
      ${FONT8x16}
      ${FONT16x32}
)

add_subdirectory(${ARCH})
