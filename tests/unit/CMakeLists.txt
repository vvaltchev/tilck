# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

if (ARCH_GTESTS)
   set_cross_compiler_gtests()
else()
   set(TOOL_GCOV "${GCOV}")
endif()

set(GTEST_SOURCE_DIR "${TCROOT}/cache/googletest/googletest")

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include/system_headers)

include_directories(${GTEST_SOURCE_DIR}/include)

# Create a static library target for kernel's arch-independent code.
add_library(
   kernel_noarch_for_test
   STATIC EXCLUDE_FROM_ALL ${KERNEL_NOARCH_SOURCES}
)

set_target_properties(

   kernel_noarch_for_test

   PROPERTIES
      COMPILE_FLAGS "${KERNEL_NO_ARCH_FLAGS}"
)


configure_file(
   ${CMAKE_SOURCE_DIR}/scripts/templates/generate_coverage_report
   ${CMAKE_BINARY_DIR}/scripts/generate_test_coverage_report
   @ONLY
)

file(
   GLOB SOURCES

   "*.c"
   "*.cpp"
   "${CMAKE_SOURCE_DIR}/common/3rd_party/datetime.c"
   "${CMAKE_SOURCE_DIR}/common/arch/${ARCH_FAMILY}/*.c"
)

# Override CMake's default executable output directory
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

add_executable(gtests EXCLUDE_FROM_ALL ${SOURCES})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-register")

string(
   CONCAT gtests_flags
   "-DTESTING -fPIC "
   " ${CLANG_OPTS} ${COMMON_FLAGS} ${GCOV_COMPILE_FLAGS}"
)

set_target_properties(

   gtests

   PROPERTIES
      COMPILE_FLAGS "${gtests_flags}"
      LINK_FLAGS "${GCOV_LINK_FLAGS}"
)

set(
   WRAPPED_SYMS

   assert_failed
   not_reached
   not_implemented
   printk
   kmutex_lock
   kmutex_unlock
)

PREPEND(WRAPPED_SYMS_LIST "-Wl,--wrap," ${WRAPPED_SYMS})
JOIN("${WRAPPED_SYMS_LIST}" ${SPACE} WRAPPINGS)

target_link_libraries(gtests ${GTEST_TC_BUILD_DIR}/libgtest.a)
target_link_libraries(gtests ${GTEST_TC_BUILD_DIR}/libgtest_main.a)
target_link_libraries(gtests pthread)
target_link_libraries(gtests "${WRAPPINGS}")
target_link_libraries(gtests kernel_noarch_for_test)
build_all_modules(gtests "_noarch")
target_link_libraries(gtests kernel_noarch_for_test)
