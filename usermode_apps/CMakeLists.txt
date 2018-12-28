# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

set_cross_compiler_userapps()

add_definitions(-DUSERMODE_APP)

set(APPS_BIN_FILES "")

set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS) # Remove -rdynamic
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g -DUSERMODE_APP")

add_custom_target(usermode_apps)
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${TCROOT}/zlib/install/include)


# add_usermode_app() has to be a macro and not a function because add_executable
# does not accept variables. An unescaped literal is required. Therefore, we
# can work-around this limitation only by going one level "up" and use a macro.
# CMake's macros are very much like C's macros.

macro(add_usermode_app)
   file(GLOB SOURCES "${ARGV1}${ARGV0}*.c")
   add_executable(${ARGV0} ${SOURCES})
   list(APPEND APPS_BIN_FILES "${CMAKE_BINARY_DIR}/usermode_apps/${ARGV0}")
   add_dependencies(usermode_apps ${ARGV0})
endmacro()

add_usermode_app(init)
add_usermode_app(termtest)
add_usermode_app(fbtest)
add_usermode_app(devshell "devshellsrc/")
target_link_libraries(devshell ${TCROOT}/zlib/install/lib/libz.a)

if (EXISTS "${TCROOT}/ncurses/install/lib/libncurses_g.a")
   add_usermode_app(ncapp)
   target_include_directories(ncapp PUBLIC ${TCROOT}/ncurses/install/include)
   target_link_libraries(ncapp ${TCROOT}/ncurses/install/lib/libncurses_g.a)
endif()

# This macro makes the integration with extra apps much easier.
# It's enough for the external app to just add a call to tilck_add_extra_app()
# with the full-path of a binary in order to that deliverable to become a file
# dependency of Tilck's main target (the image) and get that deliverable copied
# in /usr/bin (in Tilck's fatpart).
# Example: https://github.com/vvaltchev/tfblib

macro(tilck_add_extra_app)
   set(EXTRA_APPS_LIST_copy ${EXTRA_APPS_LIST})
   list(APPEND EXTRA_APPS_LIST_copy ${ARGV0})
   set(EXTRA_APPS_LIST ${EXTRA_APPS_LIST_copy})
   set(EXTRA_APPS_LIST ${EXTRA_APPS_LIST_copy} PARENT_SCOPE)
   message(STATUS "Adding EXTRA APP: ${ARGV0}")
endmacro()

add_subdirectory(extra)

set(USERMODE_APPS_FILES_LIST ${APPS_BIN_FILES} PARENT_SCOPE)
set(EXTRA_APPS_LIST ${EXTRA_APPS_LIST} PARENT_SCOPE)
