# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

set(
   KERNEL_NOARCH_SOURCES_GLOB

   "${PROJ_ROOT}/kernel/*.c"
   "${PROJ_ROOT}/kernel/*.cpp"
   "${PROJ_ROOT}/kernel/*/*.c"
   "${PROJ_ROOT}/kernel/*/*.cpp"
   "${PROJ_ROOT}/kernel/fs/*/*.c"
   "${PROJ_ROOT}/kernel/fs/*/*.cpp"
   "${PROJ_ROOT}/common/*.c"
   "${PROJ_ROOT}/common/*.cpp"
   "${PROJ_ROOT}/common/3rd_party/datetime.c"
   "${PROJ_ROOT}/common/3rd_party/crc32.c"
)

if (KERNEL_SELFTESTS)
   list(
      APPEND
      KERNEL_NOARCH_SOURCES_GLOB
      "${PROJ_ROOT}/tests/self/*.c"
   )
endif()

file(GLOB KERNEL_NOARCH_SOURCES ${GLOB_CONF_DEP} ${KERNEL_NOARCH_SOURCES_GLOB})
