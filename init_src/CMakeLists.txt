cmake_minimum_required(VERSION 3.2)

# Remove cmake's default options
SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")

file(GLOB SOURCES "*.c")

set(DIET_SRC_FLAGS "-nostdlib -m32 -isystem ${CMAKE_SOURCE_DIR}/toolchain/dietlibc-0.33/include -D__dietlibc__")

set_source_files_properties(
   ${SOURCES}
   PROPERTIES
      COMPILE_FLAGS ${DIET_SRC_FLAGS}
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${GENERAL_DEFS} ${OPT_FLAGS} ${WARN_FLAGS} -g -fno-stack-protector")

add_executable(init "${SOURCES}")
