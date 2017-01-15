cmake_minimum_required(VERSION 3.2)

file(GLOB SOURCES "*.c" "*.cpp")

set(GTEST_DIR "${CMAKE_SOURCE_DIR}/toolchain/googletest/googletest")

include_directories("../kernel/include")
include_directories("${GTEST_DIR}/include")

set(GENERAL_DEFS "${GENERAL_DEFS} -DTESTING")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${GENERAL_DEFS} ${GENERAL_CFLAGS} ${OPT_FLAGS} ${WARN_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${GENERAL_DEFS} ${GENERAL_CFLAGS} ${OPT_FLAGS} ${WARN_FLAGS}")


set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}")
add_executable(tests "${SOURCES}")

set(SYM_WRAPPINGS "-Wl,--wrap,panic\
-Wl,--wrap,assert_failed\
-Wl,--wrap,printk\
-Wl,--wrap,kbasic_virtual_alloc\
-Wl,--wrap,kbasic_virtual_free\
-Wl,--wrap,is_mapped")

set_target_properties(

   tests

   PROPERTIES
      LINK_FLAGS "${SYM_WRAPPINGS}"
)


target_link_libraries(tests "${GTEST_DIR}/libgtest.a" "${GTEST_DIR}/libgtest_main.a" pthread)
target_link_libraries(tests kernel_noarch_static_for_test)
