# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

set(
   KERNEL_NOARCH_SOURCES_GLOB

   "${CMAKE_SOURCE_DIR}/kernel/*.c"
   "${CMAKE_SOURCE_DIR}/kernel/*/*.c"
   "${CMAKE_SOURCE_DIR}/kernel/fs/*/*.c"
   "${CMAKE_SOURCE_DIR}/common/*.c"
   "${CMAKE_SOURCE_DIR}/common/3rd_party/datetime.c"
   "${CMAKE_SOURCE_DIR}/common/3rd_party/crc32.c"
)

if (KERNEL_SELFTESTS)
   list(
      APPEND
      KERNEL_NOARCH_SOURCES_GLOB
      "${CMAKE_SOURCE_DIR}/tests/self/*.c"
   )
endif()

file(GLOB KERNEL_NOARCH_SOURCES ${KERNEL_NOARCH_SOURCES_GLOB})
