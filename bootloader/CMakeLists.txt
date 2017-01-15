cmake_minimum_required(VERSION 3.2)
project (exos_bootloader ASM_NASM)

file(GLOB SOURCES "*.asm")

add_custom_command(
    OUTPUT
        boot.o
    DEPENDS
        ${SOURCES}
    COMMAND
        nasm -f bin -o boot.o ${SOURCES}
    COMMENT
        "Building the bootloader"
)

add_custom_command(
    OUTPUT
        ${CMAKE_BINARY_DIR}/bootloader.bin
    COMMAND
        cp boot.o ${CMAKE_BINARY_DIR}/bootloader.bin
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/boot.o
    COMMENT
        "Linking the bootloader"   
)

add_custom_target(
    
    bootloader ALL
    
    DEPENDS
        ${CMAKE_BINARY_DIR}/bootloader.bin
)