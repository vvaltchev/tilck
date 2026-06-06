# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

# host_ncurses (the only dependency) lives in the :distro host tier, as
# .../<distro>/ncurses/<ver>/install. It is a default toolchain package
# (scripts/pkgmgr/ncurses.rb), so a normal toolchain has it.
set(NCURSES_ROOT ${HOST_DIR_DISTRO}/ncurses/${VER_NCURSES}/install)

if (NOT EXISTS ${NCURSES_ROOT}/lib/libncursesw.a)

   message(STATUS
      "coverage-viewer: host_ncurses not found at ${NCURSES_ROOT}; "
      "skipping. Install it with: ./scripts/build_toolchain -s host_ncurses")
   return()

endif()

file(GLOB COVERAGE_VIEWER_SRC ${GLOB_CONF_DEP} "*.cpp")

add_executable(coverage-viewer ${COVERAGE_VIEWER_SRC})

set_target_properties(

   coverage-viewer

   PROPERTIES
      CXX_STANDARD 17
      CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_compile_options(coverage-viewer PRIVATE -O2 -ggdb -Wall -Wextra)

# curses.h lives under install/include/ncursesw, but it pulls in its own
# headers as <ncursesw/...>, so install/include must be on the path too
# (both dirs are what ncursesw.pc's Cflags list).
target_include_directories(

   coverage-viewer

   PRIVATE
      ${NCURSES_ROOT}/include/ncursesw
      ${NCURSES_ROOT}/include
)

# Link the static libs only: libncursesw for the curses API, libtinfo
# for the terminfo symbols it was split into (--with-termlib=tinfo).
target_link_libraries(

   coverage-viewer

   ${NCURSES_ROOT}/lib/libncursesw.a
   ${NCURSES_ROOT}/lib/libtinfo.a
)
