cmake_minimum_required(VERSION 3.2)

if (${ARCH} STREQUAL "i386")

   set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/i686-linux-gcc)

endif()

add_definitions(-DUSERMODE_APP)

set(APPS_BIN_FILES "")

set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS) # Remove -rdynamic
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g")

file(GLOB INIT_SOURCES "init*.c")
add_executable(init ${INIT_SOURCES})
list(APPEND APPS_BIN_FILES "${CMAKE_BINARY_DIR}/usermode_apps/init")

file(GLOB SHELL_SOURCES "devshell*.c")
add_executable(devshell ${SHELL_SOURCES})
list(APPEND APPS_BIN_FILES "${CMAKE_BINARY_DIR}/usermode_apps/devshell")

file(GLOB TERMTEST_SOURCES "termtest*.c")
add_executable(termtest ${TERMTEST_SOURCES})
list(APPEND APPS_BIN_FILES "${CMAKE_BINARY_DIR}/usermode_apps/termtest")
target_include_directories(termtest PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(termtest PUBLIC USERMODE_APP)

file(GLOB TERMTEST_SOURCES "fbtest*.c")
add_executable(fbtest ${TERMTEST_SOURCES})
list(APPEND APPS_BIN_FILES "${CMAKE_BINARY_DIR}/usermode_apps/fbtest")
target_include_directories(fbtest PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(fbtest PUBLIC USERMODE_APP)

add_custom_target(

   usermode_apps

   DEPENDS
      init devshell termtest fbtest
)

if (EXISTS "${TCROOT}/ncurses_install/lib/libncurses_g.a")
   file(GLOB NCAPP_SOURCES "ncapp*.c")
   add_executable(ncapp ${NCAPP_SOURCES})
   list(APPEND APPS_BIN_FILES "${CMAKE_BINARY_DIR}/usermode_apps/ncapp")
   target_include_directories(ncapp PUBLIC ${TCROOT}/ncurses_install/include)
   target_link_libraries(ncapp ${TCROOT}/ncurses_install/lib/libncurses_g.a)
   add_dependencies(usermode_apps ncapp)
endif()


set(USERMODE_APPS_FILES_LIST ${APPS_BIN_FILES} PARENT_SCOPE)
