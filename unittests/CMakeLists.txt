cmake_minimum_required(VERSION 3.2)

file(
   GLOB SOURCES

   "*.c"
   "*.cpp"
   "${CMAKE_SOURCE_DIR}/common/arch/${ARCH_FAMILY}/*.c"
)

set(GTEST_SOURCE_DIR "${TCROOT}/googletest/googletest")
set(GTEST_BUILD_DIR "${TCROOT}/gtest")

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${GTEST_SOURCE_DIR}/include)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
add_executable(gtests EXCLUDE_FROM_ALL ${SOURCES})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-register")

# if (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
#    set(CLANG_OPTS "-fsanitize=address")
#    message(STATUS "Using CLANG_OPTS: ${CLANG_OPTS}")
# endif()

set_target_properties(

   gtests

   PROPERTIES
      COMPILE_FLAGS "-DTESTING ${CLANG_OPTS} ${COMMON_FLAGS} ${GCOV_COMPILE_FLAGS}"
      LINK_FLAGS "${GCOV_LINK_FLAGS}"
)

set(
   WRAPPED_SYMS

   panic
   assert_failed
   not_reached
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

