# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

######################## COMMON CODE & DEFINITIONS #############################

set_cross_compiler_userapps()
add_definitions(-DUSERMODE_APP)

# Force static building for all the user applications
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${USERAPPS_CFLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${USERAPPS_CFLAGS}")

add_custom_target(userapps)
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${TCROOT}/zlib/install/include)

set(NCURSES_INST "${TCROOT}/ncurses/install")

# [build utils]

   macro(register_usermode_app)
      list(APPEND APPS_BIN_FILES "${CMAKE_BINARY_DIR}/userapps/${ARGV0}")
      add_dependencies(userapps ${ARGV0})
   endmacro()

   macro(add_usermode_app)
      file(GLOB SOURCES "${ARGV1}${ARGV0}*.c")
      add_executable(${ARGV0} ${SOURCES})
      register_usermode_app(${ARGV0})
   endmacro()

# [/build utils]

################################ TARGETS #######################################

# [simple apps]
   add_usermode_app(init)
   add_usermode_app(termtest)
   add_usermode_app(fbtest)
# [/simple apps]


# [devshell]
   file(
      GLOB
      DEVSHELL_SOURCES

      "devshell/*.c"
      "${CMAKE_SOURCE_DIR}/tests/system/*.c"
      "${CMAKE_SOURCE_DIR}/common/3rd_party/base64.c"
   )

   add_executable(devshell ${DEVSHELL_SOURCES})
   set(DEVSHELL_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/devshell)
   target_include_directories(devshell PUBLIC ${DEVSHELL_SRC_DIR})
   target_link_libraries(devshell ${TCROOT}/zlib/install/lib/libz.a)
   register_usermode_app(devshell)
# [/devshell]

# [ncurses test app]
   if (EXISTS ${NCURSES_INST}/lib/libncurses_g.a)
      add_usermode_app(ncapp)
      target_include_directories(ncapp PUBLIC ${NCURSES_INST}/include)
      target_link_libraries(ncapp ${NCURSES_INST}/lib/libncurses_g.a)
   endif()
# [/ncurses test app]


# [extra]
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

   set(USERAPPS_FILES_LIST ${APPS_BIN_FILES} PARENT_SCOPE)
   set(EXTRA_APPS_LIST ${EXTRA_APPS_LIST} PARENT_SCOPE)

# [/extra]
