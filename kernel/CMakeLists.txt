cmake_minimum_required(VERSION 3.2)

set(
   GENERAL_KERNEL_FLAGS_LIST

   -mno-red-zone
   -ffreestanding
   -nostdinc
   -fno-builtin
   -fno-omit-frame-pointer
)
JOIN("${GENERAL_KERNEL_FLAGS_LIST}" ${SPACE} GENERAL_KERNEL_FLAGS)

set(KERNEL_FLAGS "${COMMON_FLAGS} ${GENERAL_KERNEL_FLAGS}")

include_directories(${CMAKE_SOURCE_DIR}/kernel/include)
include_directories(${CMAKE_SOURCE_DIR}/common/include)


set(
   KERNEL_NOARCH_SOURCES_GLOB

   "${CMAKE_SOURCE_DIR}/kernel/*.c"
   "${CMAKE_SOURCE_DIR}/kernel/fs/*.c"
   "${CMAKE_SOURCE_DIR}/kernel/self_tests/*.c"
   "${CMAKE_SOURCE_DIR}/common/*.c"

   # HACK! HACK! HACK!
   "${CMAKE_SOURCE_DIR}/kernel/arch/i386/paging.c"
)

file(GLOB SOURCES ${KERNEL_NOARCH_SOURCES_GLOB})

# Creating a static library target for putting kernel's arch-independent code.

add_library(kernel_noarch_static_for_test STATIC EXCLUDE_FROM_ALL ${SOURCES})

set_target_properties(

   kernel_noarch_static_for_test

   PROPERTIES
      COMPILE_FLAGS "-DKERNEL_TEST ${KERNEL_FLAGS} ${GCOV_COMPILE_FLAGS}"
)

add_subdirectory(arch)
