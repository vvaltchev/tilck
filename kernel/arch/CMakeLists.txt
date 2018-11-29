# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

if (${KERNEL_USE_SYSTEM_CC} OR ${USE_SYSCC})

   if (${ARCH} STREQUAL "i386")

      set(CMAKE_C_FLAGS "${ARCH_GCC_FLAGS}")
      set(CMAKE_CXX_FLAGS "${ARCH_GCC_FLAGS}")

      if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
         set(CMAKE_ASM_FLAGS "${ARCH_GCC_FLAGS}")
      else()
         # If system's compiler (CC) is Clang, we cannot use it for compiling
         # our assembly code, because LLVM's assembler (llvm-as) is not 100%
         # compatible with GAS's intel-syntax dialect.
         set(CMAKE_ASM_COMPILER ${GCC_TOOLCHAIN}/${ARCH_GCC_TC}-linux-gcc)
      endif()

   endif()

   set(OBJCOPY "${SYS_OBJCOPY}")
   set(STRIP "${SYS_STRIP}")

   if (KERNEL_TC_ISYSTEM_WITH_SYSTEM_CC)
      include_directories(SYSTEM ${GCC_TC_ISYSTEM})
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
