cmake_minimum_required(VERSION 3.2)

file(GLOB SOURCES "*.c" "*.cpp")

set(GTEST_DIR "${CMAKE_SOURCE_DIR}/toolchain/googletest/googletest")

include_directories("${CMAKE_SOURCE_DIR}/kernel/include")
include_directories("${GTEST_DIR}/include")

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
add_executable(tests EXCLUDE_FROM_ALL ${SOURCES})

set_target_properties(

   tests

   PROPERTIES
      COMPILE_FLAGS "-DTESTING ${COMMON_FLAGS} ${GCOV_COMPILE_FLAGS}"
      LINK_FLAGS "${GCOV_LINK_FLAGS}"
)

set(
   WRAPPED_SYMS

   panic
   assert_failed
   not_reached
   printk
   kbasic_virtual_alloc
   kbasic_virtual_free
   is_mapped
)

PREPEND(WRAPPED_SYMS_LIST "-Wl,--wrap," ${WRAPPED_SYMS})
JOIN("${WRAPPED_SYMS_LIST}" ${SPACE} WRAPPINGS)

target_link_libraries(tests ${GTEST_DIR}/libgtest.a)
target_link_libraries(tests ${GTEST_DIR}/libgtest_main.a)
target_link_libraries(tests pthread)
target_link_libraries(tests kernel_noarch_static_for_test "${WRAPPINGS}")
