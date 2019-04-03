# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

if (${ARCH_GTESTS})
   set_cross_compiler()
endif()

set(
   GENERAL_KERNEL_FLAGS_LIST

   -D__TILCK_KERNEL__

   -mno-red-zone
   -ffreestanding
   -fno-builtin
   -fno-omit-frame-pointer
   -fwrapv
   -Wno-unused-label
   -Wvla
)

if (WCONV)
   list(APPEND GENERAL_KERNEL_FLAGS_LIST "-Wconversion")
endif()

if (FLAG_NO_TREE_LOOP_VEC_SUPPORTED)
   list(APPEND GENERAL_KERNEL_FLAGS_LIST -fno-tree-loop-vectorize)
endif()

if (FLAG_NO_TREE_VEC_SUPPORTED)
   list(APPEND GENERAL_KERNEL_FLAGS_LIST -fno-tree-vectorize)
endif()

if (FLAG_NO_TREE_SLP_VEC_SUPPORTED)
   list(APPEND GENERAL_KERNEL_FLAGS_LIST -fno-tree-slp-vectorize)
endif()


JOIN("${GENERAL_KERNEL_FLAGS_LIST}" ${SPACE} GENERAL_KERNEL_FLAGS)

set(KERNEL_FLAGS "${COMMON_FLAGS} ${GENERAL_KERNEL_FLAGS}")

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include/system_headers)

set(
   KERNEL_NOARCH_SOURCES_GLOB

   "${CMAKE_SOURCE_DIR}/kernel/*.c"
   "${CMAKE_SOURCE_DIR}/kernel/*/*.c"
   "${CMAKE_SOURCE_DIR}/kernel/char/*/*.c"
   "${CMAKE_SOURCE_DIR}/common/*.c"
)

if (KERNEL_SELFTESTS)
   list(
         APPEND
         KERNEL_NOARCH_SOURCES_GLOB
         "${CMAKE_SOURCE_DIR}/tests/self/*.c"
   )
endif()

file(GLOB SOURCES ${KERNEL_NOARCH_SOURCES_GLOB})

# Creating a static library target for putting kernel's arch-independent code.

add_library(kernel_noarch_static_for_test STATIC EXCLUDE_FROM_ALL ${SOURCES})

string(CONCAT no_arch_flags
         "-DKERNEL_TEST -fPIC "
         "${KERNEL_FLAGS} ${GCOV_COMPILE_FLAGS}")

set_target_properties(

   kernel_noarch_static_for_test

   PROPERTIES
      COMPILE_FLAGS "${no_arch_flags}"
)

add_subdirectory(arch)
