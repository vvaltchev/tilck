# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

if (${ARCH} STREQUAL "i386")

   set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/i686-linux-gcc)

endif()

add_definitions(-DUSERMODE_APP)

set(APPS_BIN_FILES "")

set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS) # Remove -rdynamic
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g -DUSERMODE_APP")

add_custom_target(usermode_apps)
include_directories(${CMAKE_SOURCE_DIR}/include)

macro(add_usermode_app)
   file(GLOB SOURCES "${ARGV0}*.c")
   add_executable(${ARGV0} ${SOURCES})
   list(APPEND APPS_BIN_FILES "${CMAKE_BINARY_DIR}/usermode_apps/${ARGV0}")
   add_dependencies(usermode_apps ${ARGV0})
endmacro()

add_usermode_app(init)
add_usermode_app(devshell)
add_usermode_app(termtest)
add_usermode_app(fbtest)

if (EXISTS "${TCROOT}/ncurses_install/lib/libncurses_g.a")
   add_usermode_app(ncapp)
   target_include_directories(ncapp PUBLIC ${TCROOT}/ncurses_install/include)
   target_link_libraries(ncapp ${TCROOT}/ncurses_install/lib/libncurses_g.a)
endif()

set(USERMODE_APPS_FILES_LIST ${APPS_BIN_FILES} PARENT_SCOPE)
