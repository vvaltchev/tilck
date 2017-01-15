cmake_minimum_required(VERSION 3.2)
project (exos C ASM_NASM)

set(CMAKE_VERBOSE_MAKEFILE on)

add_subdirectory(bootloader)
add_subdirectory(kernel)

set(IMG_FILE ${CMAKE_BINARY_DIR}/exos.img)


add_custom_command(
    
    OUTPUT
        ${IMG_FILE}
    COMMAND
        ${CMAKE_SOURCE_DIR}/build_scripts/create_empty_img_if_necessary.sh ${IMG_FILE} 1440
    COMMAND
        dd status=none conv=notrunc if=${CMAKE_BINARY_DIR}/bootloader.bin of=${IMG_FILE}
    COMMAND
        dd status=none conv=notrunc if=${CMAKE_BINARY_DIR}/kernel.bin of=$@ seek=4 obs=1024 ibs=1024   
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
