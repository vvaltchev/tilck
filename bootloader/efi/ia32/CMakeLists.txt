
cmake_minimum_required(VERSION 3.2)

set(EFI_ARCH ia32)
set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/i686-linux-gcc)

file(GLOB SOURCES "../*.c")
include (../GenericBuild.cmake)
