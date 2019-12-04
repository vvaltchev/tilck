# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

if (${ARCH_GTESTS})
   set_cross_compiler()
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include/system_headers)

set(
   KERNEL_NOARCH_SOURCES_GLOB

   "${CMAKE_SOURCE_DIR}/kernel/*.c"
   "${CMAKE_SOURCE_DIR}/kernel/*/*.c"
   "${CMAKE_SOURCE_DIR}/kernel/fs/*/*.c"
   "${CMAKE_SOURCE_DIR}/common/*.c"
   "${CMAKE_SOURCE_DIR}/common/3rd_party/datetime.c"
)

if (KERNEL_SELFTESTS)
   list(
      APPEND
      KERNEL_NOARCH_SOURCES_GLOB
      "${CMAKE_SOURCE_DIR}/tests/self/*.c"
   )
endif()

file(GLOB SOURCES ${KERNEL_NOARCH_SOURCES_GLOB})

# Create a static library target for kernel's arch-independent code.
add_library(kernel_noarch_for_test STATIC EXCLUDE_FROM_ALL ${SOURCES})

set_target_properties(

   kernel_noarch_for_test

   PROPERTIES
      COMPILE_FLAGS "${KERNEL_NO_ARCH_FLAGS}"
)

add_subdirectory(arch)
