# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

include_directories(${CMAKE_SOURCE_DIR}/include)
add_definitions(-DUSERMODE_APP)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -ggdb")

file(
   GLOB FAT_GET_USED_BYTES_SRC
   "fat_get_used_bytes.c"
   "${CMAKE_SOURCE_DIR}/common/*.c"
)

add_executable(fat_get_used_bytes ${FAT_GET_USED_BYTES_SRC})

add_executable(elfhack "elfhack.c")
add_executable(pbm2text "pbm2text.c")
