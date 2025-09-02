# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include/system_headers)
include_directories(${TCROOT}/noarch/acpica/${VER_ACPICA})

add_subdirectory(arch)
