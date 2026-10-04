# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

include_directories(${CMAKE_SOURCE_DIR}/include)
add_definitions(-DUSERMODE_APP)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -ggdb")

file(
   GLOB FATHACK_SRC
   ${GLOB_CONF_DEP}
   "fathack.c"
   "${CMAKE_SOURCE_DIR}/common/*.c"
   "${CMAKE_SOURCE_DIR}/common/*.cpp"
)

add_executable(fathack ${FATHACK_SRC})
add_executable(pnm2text "pnm2text.c")
add_executable(mbrhack "mbrhack.c")
add_executable(gen_config "gen_config.cpp")

if (APPLE)
   add_executable(machohack "machohack.c")
endif()
