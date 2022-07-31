# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

if (ARCH_GTESTS)
   set_cross_compiler_gtests()
else()
   set(TOOL_GCOV "${GCOV}")
endif()

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include/system_headers)

include_directories(${GTEST_SOURCE_DIR}/include)
include_directories(${GMOCK_SOURCE_DIR}/include)

# Create a static library target for kernel's arch-independent code.
add_library(

   kernel_for_test

   STATIC EXCLUDE_FROM_ALL

   ${KERNEL_NOARCH_SOURCES}
   ${CMAKE_SOURCE_DIR}/other/mock_experiments.c
)

set_target_properties(

   kernel_for_test

   PROPERTIES
      COMPILE_FLAGS "${KERNEL_NO_ARCH_FLAGS}"
)

set(LIB_KERNEL "libkernel_test_patched.a")
set(TOOL_WS ${CMAKE_BINARY_DIR}/scripts/weaken_syms)

add_custom_command(

   OUTPUT
      ${LIB_KERNEL}
   COMMAND
      cp libkernel_for_test.a ${LIB_KERNEL}
   COMMAND
      ${TOOL_WS} ${LIB_KERNEL} experiment_bar experiment_foo
   DEPENDS
      kernel_for_test
      ${TOOL_WS}
      elfhack32
      elfhack64
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
   ${CMAKE_SOURCE_DIR}/scripts/templates/generate_coverage_report
   ${CMAKE_BINARY_DIR}/scripts/generate_test_coverage_report
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
)

JOIN("${GTESTS_FLAGS_LIST}" ${SPACE} GTESTS_FLAGS)

set_target_properties(

   gtests

   PROPERTIES
      COMPILE_FLAGS "${GTESTS_FLAGS}"
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
   fat_ramdisk_prepare_for_mmap
   wth_create_thread_for
   wth_wakeup
   check_in_irq_handler
)

PREPEND(WRAPPED_SYMS_LIST "-Wl,--wrap," ${WRAPPED_SYMS})
JOIN("${WRAPPED_SYMS_LIST}" ${SPACE} WRAPPINGS)

target_link_libraries(gtests ${GTEST_TC_BUILD_DIR}/lib/libgtest.a)
target_link_libraries(gtests ${GTEST_TC_BUILD_DIR}/lib/libgtest_main.a)
target_link_libraries(gtests ${GMOCK_TC_BUILD_DIR}/lib/libgmock.a)
target_link_libraries(gtests ${GMOCK_TC_BUILD_DIR}/lib/libgmock_main.a)
target_link_libraries(gtests pthread)
target_link_libraries(gtests "${WRAPPINGS}")
target_link_libraries(gtests ${CMAKE_CURRENT_BINARY_DIR}/${LIB_KERNEL})
build_all_modules(gtests "_noarch")
target_link_libraries(gtests ${CMAKE_CURRENT_BINARY_DIR}/${LIB_KERNEL})
