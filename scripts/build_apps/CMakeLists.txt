cmake_minimum_required(VERSION 3.2)

include_directories(${CMAKE_SOURCE_DIR}/include)
add_definitions(-DUSERMODE_APP)

file(GLOB ELFHACK_SOURCES "elfhack.c")

add_executable(elfhack ${ELFHACK_SOURCES})

file(
   GLOB FAT_GET_USED_BYTES_SRC
   "fat_get_used_bytes.c"
   "${CMAKE_SOURCE_DIR}/common/*.c"
)

add_executable(fat_get_used_bytes ${FAT_GET_USED_BYTES_SRC})
