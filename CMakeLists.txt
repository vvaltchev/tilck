cmake_minimum_required(VERSION 3.2)
project (exos C ASM_NASM)

set(CMAKE_VERBOSE_MAKEFILE on)

add_subdirectory(bootloader)
add_subdirectory(kernel)

set(IMG_FILE ${CMAKE_BINARY_DIR}/exos.img)


add_custom_target(
    
    exos_image ALL
    
    COMMAND
        dd status=none if=/dev/zero of=${IMG_FILE} obs=512 ibs=512 count=2880
    COMMAND
        dd status=none conv=notrunc if=${CMAKE_BINARY_DIR}/bootloader.bin of=${IMG_FILE}
    COMMAND
        dd status=none conv=notrunc if=${CMAKE_BINARY_DIR}/kernel.bin of=$@ seek=4 obs=1024 ibs=1024   
    DEPENDS
        bootloader.bin kernel.bin
    COMMENT
        "Creating the bootable image file"
    )
