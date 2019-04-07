# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

if (ARCH_GTESTS)
   set_cross_compiler_gtests()
else()
   set(TOOL_GCOV "${GCOV}")
endif()

configure_file(
   ${CMAKE_SOURCE_DIR}/scripts/templates/generate_coverage_report
   ${CMAKE_BINARY_DIR}/scripts/generate_test_coverage_report
   @ONLY
)

file(
   GLOB SOURCES

   "*.c"
   "*.cpp"
   "${CMAKE_SOURCE_DIR}/common/arch/${ARCH_FAMILY}/*.c"
)

set(GTEST_SOURCE_DIR "${TCROOT}/googletest/googletest")

if (${ARCH_GTESTS})
   set(GTEST_BUILD_DIR "${TCROOT}/gtest_arch")
else()
   set(GTEST_BUILD_DIR "${TCROOT}/gtest")
endif()

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${GTEST_SOURCE_DIR}/include)

# Override CMake's default executable output directory
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

add_executable(gtests EXCLUDE_FROM_ALL ${SOURCES})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-register")

# if (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
#    set(CLANG_OPTS "-fsanitize=address")
#    message(STATUS "Using CLANG_OPTS: ${CLANG_OPTS}")
# endif()

string(CONCAT gtests_flags
         "-DTESTING -fPIC "
         " ${CLANG_OPTS} ${COMMON_FLAGS} ${GCOV_COMPILE_FLAGS}")

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

# if (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
#    target_link_libraries(gtests asan)
# endif()

target_link_libraries(gtests ${GTEST_BUILD_DIR}/libgtest.a)
target_link_libraries(gtests ${GTEST_BUILD_DIR}/libgtest_main.a)
target_link_libraries(gtests pthread)
target_link_libraries(gtests kernel_noarch_static_for_test "${WRAPPINGS}")
