# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

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


# B2O = Binary to Object file [options]
list(APPEND B2O -O ${ARCH_ELF_NAME} -B ${ARCH} -I binary)

foreach(font_file ${font_files})

   get_filename_component(font_name ${font_file} NAME_WE)
   get_filename_component(font_dir ${font_file} DIRECTORY)
   set(obj_file ${CMAKE_CURRENT_BINARY_DIR}/${font_name}.o)

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

add_dependencies(tilck_unstripped fonts)
target_link_libraries(tilck_unstripped ${CMAKE_CURRENT_BINARY_DIR}/font8x16.o)
target_link_libraries(tilck_unstripped ${CMAKE_CURRENT_BINARY_DIR}/font16x32.o)
