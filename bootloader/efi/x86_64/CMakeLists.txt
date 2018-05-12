cmake_minimum_required(VERSION 3.2)

set(EFI_ARCH x86_64)

file(GLOB SOURCES "../*.c" "*.S")
include (../GenericBuild.cmake)


