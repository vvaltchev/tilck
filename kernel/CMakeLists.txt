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
   -Wno-unused-label

   # NOTE: DO NOT USE gcc's options like -mavx and related (-msse etc.):
   # That will allow gcc even in __asm__ volatile statements to transform
   # instructions like 'movdqa' in the newer version (AVX) 'vmovdqa'. Clearly
   # is a problem since we check at runtime if AVX is available. In order to be
   # sure that gcc does NOT mess-up with us, we have to always use inline
   # assembly and none of the -mXXX gcc options. They're good in case one is
   # SURE that the given feature is supported, not in case it MIGHT BE.
)

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
   "${CMAKE_SOURCE_DIR}/common/*.c"
)

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
