# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

if (${KERNEL_SYSCC} OR ${USE_SYSCC})

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

   set(TOOL_OBJCOPY "${OBJCOPY}")
   set(TOOL_STRIP "${STRIP}")

   if (KERNEL_FORCE_TC_ISYSTEM)
      include_directories(SYSTEM ${GCC_TC_ISYSTEM})
   endif()

else()
   set_cross_compiler()
endif()

configure_file(
   ${CMAKE_SOURCE_DIR}/scripts/templates/generate_coverage_report
   ${CMAKE_BINARY_DIR}/scripts/generate_kernel_coverage_report
   @ONLY
)

# B2O = Binary to Object file [options]
list(APPEND B2O -O ${ARCH_ELF_NAME} -B ${ARCH} -I binary)

set(HAVE_ALT_FONTS 0)

if (EXISTS ${CMAKE_SOURCE_DIR}/other/alt_fonts/font16x32.psf)
   if (EXISTS ${CMAKE_SOURCE_DIR}/other/alt_fonts/font8x16.psf)
      set(HAVE_ALT_FONTS 1)
   endif()
endif()

if (${HAVE_ALT_FONTS} AND ${FB_CONSOLE_USE_ALT_FONTS})
   file(GLOB font_files "${CMAKE_SOURCE_DIR}/other/alt_fonts/*.psf")
else()
   file(GLOB font_files "${CMAKE_SOURCE_DIR}/modules/fb/*.psf")
endif()

foreach(font_file ${font_files})

   get_filename_component(font_name ${font_file} NAME_WE)
   get_filename_component(font_dir ${font_file} DIRECTORY)
   set(obj_file ${CMAKE_CURRENT_BINARY_DIR}/${ARCH}/${font_name}.o)

   add_custom_command(

      OUTPUT
         ${obj_file}
      COMMAND
         ${TOOL_OBJCOPY} ${B2O} ${font_name}.psf ${obj_file}
      WORKING_DIRECTORY
         ${font_dir}
      DEPENDS
         ${font_file}
      COMMENT
         "Copy into ELF object file: ${font_name}.psf"
   )

   list(APPEND FONT_OBJ_FILES_LIST ${obj_file})

endforeach()

add_custom_target(

    fonts

    DEPENDS
      ${FONT_OBJ_FILES_LIST}
)

add_subdirectory(${ARCH})
