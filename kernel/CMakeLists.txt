cmake_minimum_required(VERSION 3.2)
project (exos_kernel C ASM_NASM)

set(CMAKE_VERBOSE_MAKEFILE on)

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")

# Do not use -rdynamic
SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")

set(OPT_FLAGS "-O0 -fno-inline-functions")
set(WARN_FLAGS "-Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Werror")

set(GENERAL_KERNEL_FLAGS "\
-std=c11 \
-mno-red-zone \
-fvisibility=default \
-ffreestanding -g -nostdinc -fno-builtin \
-fno-asynchronous-unwind-tables \
-fno-zero-initialized-in-bss \
-fno-exceptions")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OPT_FLAGS} ${WARN_FLAGS} ${GENERAL_KERNEL_FLAGS}")
add_definitions("-DDEBUG")



include_directories("include")

file(GLOB SOURCES "i386/*.asm" "i386/*.c" "*.c")


set(ARCH_FLAGS "-m32 -march=i686")
set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> -f elf32 -o <OBJECT> <SOURCE>")

set_source_files_properties(${SOURCES} PROPERTIES COMPILE_FLAGS ${ARCH_FLAGS})

add_executable(elf_kernel "${SOURCES}")
set_target_properties(elf_kernel PROPERTIES LINK_FLAGS "${ARCH_FLAGS} -Wl,-Ttext=0xC0100000 -nostdlib")
target_link_libraries(elf_kernel gcc)
