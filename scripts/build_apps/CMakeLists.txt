cmake_minimum_required(VERSION 3.2)

file(GLOB SOURCES "elfhack.c")

add_executable(

   elfhack

   ${SOURCES}
)
