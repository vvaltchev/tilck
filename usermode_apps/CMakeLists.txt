cmake_minimum_required(VERSION 3.2)

if (${ARCH} STREQUAL "i386")

   set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/i686-linux-gcc)

endif()

include_directories(SYSTEM ${DIET_LIBC_PATH}/include)

set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS) # Remove -rdynamic
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g")

file(GLOB INIT_SOURCES "init*.c")
add_executable(init ${INIT_SOURCES})

file(GLOB SHELL_SOURCES "shell*.c")
add_executable(shell ${SHELL_SOURCES})

file(GLOB TERMTEST_SOURCES "termtest*.c")
add_executable(termtest ${TERMTEST_SOURCES})

