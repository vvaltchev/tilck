# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

project(kernel LANGUAGES C CXX ASM)

include(${TILCK_CONFIG_FILE})

include(CheckCCompilerFlag)
set(GLOB_CONF_DEP CONFIGURE_DEPENDS)
set(BUILD_SHARED_LIBRARIES off)           # disable shared libs globally
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)    # disable -rdynamic globally
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)  # disable -rdynamic globally

include(${PROJ_ROOT}/other/cmake/compiler_flags.cmake)
include(${PROJ_ROOT}/other/cmake/utils.cmake)
include(${PROJ_ROOT}/other/cmake/errors.cmake)
include(${PROJ_ROOT}/kernel/sources.cmake)

include_directories(${PROJ_ROOT}/include)
include_directories(${PROJ_ROOT}/include/system_headers)
include_directories(${TCROOT}/noarch/acpica/${VER_ACPICA})
include_directories(${BUILD_DIR})

# Include the funcs for building the kernel modules, used both by the kernel
# and the gtests targets.

include(${PROJ_ROOT}/other/cmake/wrapped_syms.cmake)
include(${PROJ_ROOT}/other/cmake/build_modules.cmake)

set(ELFHACK ${BUILD_APPS}/elfhack${ARCH_BITS})

add_subdirectory(arch)
