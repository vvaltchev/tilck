# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

project(tilck_gtests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${TILCK_CONFIG_FILE})

set(GLOB_CONF_DEP CONFIGURE_DEPENDS)
set(BUILD_SHARED_LIBRARIES off)
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)

if (APPLE)
   # macOS ar/ranlib warn about .o files with no symbols (e.g. ubsan.c.o,
   # se_fault_resumable.c.o which compile to nothing in test builds).
   # Use 'S' flag on ar to skip the automatic ranlib, then run ranlib
   # explicitly with -no_warning_for_no_symbols.
   set(CMAKE_C_ARCHIVE_CREATE
      "<CMAKE_AR> qcS <TARGET> <LINK_FLAGS> <OBJECTS>")
   set(CMAKE_CXX_ARCHIVE_CREATE
      "<CMAKE_AR> qcS <TARGET> <LINK_FLAGS> <OBJECTS>")
   set(CMAKE_C_ARCHIVE_FINISH
      "<CMAKE_RANLIB> -no_warning_for_no_symbols <TARGET>")
   set(CMAKE_CXX_ARCHIVE_FINISH
      "<CMAKE_RANLIB> -no_warning_for_no_symbols <TARGET>")
endif()

# Load kernel-private option defaults (KRN_*). The root project forwards any
# user overrides via -DKRN_<option>=<value> on the cmake command line, which
# take precedence over these CACHE defaults.
include(${PROJ_ROOT}/other/cmake/kernel_options.cmake)

# Load utility modules needed by the build
include(${PROJ_ROOT}/other/cmake/utils.cmake)
include(${PROJ_ROOT}/other/cmake/errors.cmake)
include(${PROJ_ROOT}/other/cmake/compiler_flags.cmake)
include(${PROJ_ROOT}/other/cmake/wrapped_syms.cmake)
include(${PROJ_ROOT}/other/cmake/build_modules.cmake)
include(${PROJ_ROOT}/kernel/sources.cmake)

# Generate kernel-private config headers (config_sched.h, config_userlim.h, etc.)
# These contain KRN_* values needed by the kernel sources we compile.
file(GLOB _kernel_config_glob ${GLOB_CONF_DEP}
     "${PROJ_ROOT}/config/kernel/*.h")

foreach(_config_path ${_kernel_config_glob})
   get_filename_component(_config_name ${_config_path} NAME_WE)
   smart_config_file(
      ${_config_path}
      ${CMAKE_BINARY_DIR}/tilck_gen_headers/${_config_name}.h
   )
endforeach()

if ("${GCOV}" STREQUAL "")
   set(TOOL_GCOV "gcov")
else()
   set(TOOL_GCOV "${GCOV}")
endif()

set(GTEST_INSTALL_DIR ${HOST_DIR}/gtest/${VER_GTEST}/install)

include_directories(${TCROOT}/noarch/gnuefi/${VER_GNUEFI}/inc)

# Generated config headers (kernel-private + generated_config.h) live in
# ${CMAKE_BINARY_DIR}/tilck_gen_headers/. Shared config headers from the
# root (config_global.h, etc.) live in ${BUILD_DIR}/tilck_gen_headers/.
include_directories(BEFORE ${CMAKE_BINARY_DIR})
include_directories(${BUILD_DIR})

include_directories(${PROJ_ROOT})
include_directories(${PROJ_ROOT}/include)
include_directories(${PROJ_ROOT}/include/system_headers)
include_directories(${TCROOT}/noarch/acpica/${VER_ACPICA})

include_directories(${GTEST_INSTALL_DIR}/include)


# Create a static library target for kernel's arch-independent code.
add_library(

   kernel_for_test

   STATIC

   ${KERNEL_NOARCH_SOURCES}
   ${PROJ_ROOT}/other/mock_experiments.c
)

set_target_properties(

   kernel_for_test

   PROPERTIES
      COMPILE_FLAGS "${KERNEL_NO_ARCH_FLAGS}"
)

set(LIB_KERNEL "libkernel_test_patched.a")

if (APPLE)
   set(_ws_extra_deps ${BUILD_APPS}/machohack)
else()
   set(_ws_extra_deps ${BUILD_APPS}/elfhack32 ${BUILD_APPS}/elfhack64)
endif()

add_custom_command(

   OUTPUT
      ${LIB_KERNEL}
   COMMAND
      cp libkernel_for_test.a ${LIB_KERNEL}
   COMMAND
      ${TOOL_WS} ${LIB_KERNEL} ${WRAPPED_SYMS}
   DEPENDS
      kernel_for_test
      ${TOOL_WS}
      ${_ws_extra_deps}
   COMMENT
      "Patching the kernel archive to allow wrapping of symbols in the same TU"
   VERBATIM
)

add_custom_target(

   kernel_test_patched

   DEPENDS
      ${LIB_KERNEL}
)

smart_config_file(
   ${PROJ_ROOT}/scripts/templates/generate_coverage_report
   ${CMAKE_BINARY_DIR}/scripts/generate_test_coverage_report
)

if (${HOST_ARCH_FAMILY} STREQUAL "generic_x86")

   file(
      GLOB SOURCES

      "${PROJ_ROOT}/tests/unit/*.c"
      "${PROJ_ROOT}/tests/unit/*.cpp"
      "${PROJ_ROOT}/common/3rd_party/datetime.c"
      "${PROJ_ROOT}/common/arch/${HOST_ARCH_FAMILY}/*.c"
   )

else()

   file(
      GLOB SOURCES

      "${PROJ_ROOT}/tests/unit/*.c"
      "${PROJ_ROOT}/tests/unit/*.cpp"
      "${PROJ_ROOT}/common/3rd_party/datetime.c"
   )

endif()

# Place the gtests executable in the root build directory
set(EXECUTABLE_OUTPUT_PATH ${BUILD_DIR})

add_executable(gtests ${SOURCES})
add_dependencies(gtests kernel_test_patched)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-register")
set(GTESTS_FLAGS_LIST "")

list(
   APPEND GTESTS_FLAGS_LIST

   -DTESTING
   -fPIC
   -fno-strict-aliasing

   ${CLANG_OPTS}
   ${COMMON_FLAGS}
   ${GCOV_COMPILE_FLAGS}

   -Wno-format-security
   -Wno-deprecated-declarations
)

JOIN("${GTESTS_FLAGS_LIST}" ${SPACE} GTESTS_FLAGS)

set_target_properties(

   gtests

   PROPERTIES
      COMPILE_FLAGS "${GTESTS_FLAGS}"
      LINK_FLAGS "${GCOV_LINK_FLAGS}"
)

target_link_libraries(gtests ${GTEST_INSTALL_DIR}/lib/libgtest.a)
target_link_libraries(gtests ${GTEST_INSTALL_DIR}/lib/libgtest_main.a)
target_link_libraries(gtests ${GTEST_INSTALL_DIR}/lib/libgmock.a)
target_link_libraries(gtests ${GTEST_INSTALL_DIR}/lib/libgmock_main.a)
target_link_libraries(gtests pthread)
target_link_libraries(gtests ${CMAKE_CURRENT_BINARY_DIR}/${LIB_KERNEL})
build_all_modules(gtests "_noarch" TRUE)

if (NOT APPLE)
   # ELF linkers resolve left-to-right; repeat the kernel lib after modules
   # so that module references to kernel symbols are satisfied.
   target_link_libraries(gtests ${CMAKE_CURRENT_BINARY_DIR}/${LIB_KERNEL})
endif()
