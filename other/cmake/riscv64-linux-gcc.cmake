# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

get_filename_component(CURR_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
set(PROJ_ROOT ${CURR_DIR}/../..)

# Remove -rdynamic
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)

include(${PROJ_ROOT}/other/cmake/utils.cmake)
set_cross_compiler_internal(${GCC_TOOLCHAIN_BIN} riscv64)

