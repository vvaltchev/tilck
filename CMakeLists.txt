cmake_minimum_required(VERSION 3.2)
project (exos C ASM_NASM)

set(CMAKE_VERBOSE_MAKEFILE on)

# Generic CMake Utilities

set(SPACE " ")

function(JOIN VALUES GLUE OUTPUT)
  string (REPLACE ";" "${GLUE}" _TMP_STR "${VALUES}")
  set (${OUTPUT} "${_TMP_STR}" PARENT_SCOPE)
endfunction()

####################################

set(ARCH i386)
set(ARCH_FLAGS "-m32 -march=i686")

set(GENERAL_DEFS_LIST
   -DDEBUG
)

set(OPT_FLAGS_LIST
   -O0
   -fno-inline-functions
)

set(WARN_FLAGS_LIST
   -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Werror
)

JOIN("${GENERAL_DEFS_LIST}" "${SPACE}" GENERAL_DEFS)
JOIN("${OPT_FLAGS_LIST}" "${SPACE}" OPT_FLAGS)
JOIN("${WARN_FLAGS_LIST}" "${SPACE}" WARN_FLAGS)



add_subdirectory(bootloader)
add_subdirectory(kernel)
add_subdirectory(init_src)

set(IMG_FILE ${CMAKE_BINARY_DIR}/exos.img)


add_custom_command(

    OUTPUT
        ${IMG_FILE}
    COMMAND
        ${CMAKE_SOURCE_DIR}/build_scripts/create_empty_img_if_necessary.sh ${IMG_FILE} 1440
    COMMAND
        dd status=none conv=notrunc if=${CMAKE_BINARY_DIR}/bootloader.bin of=${IMG_FILE}
    COMMAND
        dd status=none conv=notrunc if=${CMAKE_BINARY_DIR}/kernel.bin of=${IMG_FILE} seek=4 obs=1024 ibs=1024
    DEPENDS
        # We MUST depend on BOTH the wrapper-target AND its OUTPUT in order to this
        # to be re-build when the *.bin files are re-built.
        bootloader kernel ${CMAKE_BINARY_DIR}/bootloader.bin ${CMAKE_BINARY_DIR}/kernel.bin
    COMMENT
        "Creating the bootable image file"
    VERBATIM
)



add_custom_target(

    exos_image ALL

    DEPENDS
        ${IMG_FILE}
)
