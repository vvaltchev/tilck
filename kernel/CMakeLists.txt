# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

project(kernel LANGUAGES C CXX ASM)

include(${TILCK_CONFIG_FILE})

include(CheckCCompilerFlag)
set(GLOB_CONF_DEP CONFIGURE_DEPENDS)
set(BUILD_SHARED_LIBRARIES off)           # disable shared libs globally
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)    # disable -rdynamic globally
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)  # disable -rdynamic globally

include(${PROJ_ROOT}/other/cmake/utils.cmake)
include(${PROJ_ROOT}/other/cmake/errors.cmake)
include(${PROJ_ROOT}/other/cmake/compiler_flags.cmake)
include(${PROJ_ROOT}/kernel/sources.cmake)

include_directories(${PROJ_ROOT}/include)
include_directories(${PROJ_ROOT}/include/system_headers)
include_directories(${TCROOT}/noarch/acpica/${VER_ACPICA})

# Kernel-private generated headers shadow shared ones via include path order.
# CMAKE_BINARY_DIR (kernel's build dir) MUST come before BUILD_DIR (root's).
include_directories(${CMAKE_BINARY_DIR})
include_directories(${BUILD_DIR})

# Include the funcs for building the kernel modules, used both by the kernel
# and the gtests targets.

include(${PROJ_ROOT}/other/cmake/wrapped_syms.cmake)
include(${PROJ_ROOT}/other/cmake/build_modules.cmake)

set(ELFHACK ${BUILD_APPS}/elfhack${ARCH_BITS})

include(${PROJ_ROOT}/other/cmake/kernel_options.cmake)

# KRN_TINY_KERNEL validation checks

if (KRN_TINY_KERNEL)

   if (NOT ${CMAKE_BUILD_TYPE} STREQUAL "MinSizeRel")
      message(FATAL_ERROR "KRN_TINY_KERNEL=1 requires CMAKE_BUILD_TYPE=MinSizeRel")
   endif()

   if (KERNEL_GCOV)
      message(FATAL_ERROR "KRN_TINY_KERNEL=1 requires KERNEL_GCOV=0")
   endif()

   if (KERNEL_SELFTESTS)
      message(WARNING "KRN_TINY_KERNEL=1, expected KERNEL_SELFTESTS=0")
   endif()

   if (KRN_SYMBOLS)
      message(WARNING "KRN_TINY_KERNEL=1, expected KRN_SYMBOLS=0")
   endif()

   if (KRN_SHOW_LOGO)
      message(WARNING "KRN_TINY_KERNEL=1, expected KRN_SHOW_LOGO=0")
   endif()

   if (KRN_PCI_VENDORS_LIST)
      message(WARNING "KRN_TINY_KERNEL=1, expected KRN_PCI_VENDORS_LIST=0")
   endif()

endif()

# Print kernel-private options
list(
   APPEND kernel_private_opts_list

   KRN_TIMER_HZ
   KRN_USER_STACK_PAGES
   KRN_MAX_HANDLES
   KRN_FBCON_BIGFONT_THR
   KRN_TERM_SCROLL_LINES
   KRN_KMALLOC_FIRST_HEAP_SIZE_KB
   KRN_KMALLOC_FIRST_HEAP_SIZE_KB_VAL

   KRN_TRACK_NESTED_INTERR
   KRN_STACK_ISOLATION
   KRN_FB_CONSOLE_BANNER
   KRN_FB_CONSOLE_CURSOR_BLINK
   KRN_SHOW_LOGO
   KRN_SYMBOLS
   KRN_PRINTK_ON_CURR_TTY
   KRN_CLOCK_DRIFT_COMP
   KRN_TRACE_PRINTK_ON_BOOT

   KRN_PAGE_FAULT_PRINTK
   KRN_NO_SYS_WARN
   KRN_BIG_IO_BUF
   KRN_KMALLOC_HEAVY_STATS
   KRN_KMALLOC_FREE_MEM_POISONING
   KRN_KMALLOC_SUPPORT_DEBUG_LOG
   KRN_KMALLOC_SUPPORT_LEAK_DETECTOR
   KRN_FB_CONSOLE_USE_ALT_FONTS
   KRN_RESCHED_ENABLE_PREEMPT
   KRN_MINIMAL_TIME_SLICE
   KRN_HANG_DETECTION
   KRN_TINY_KERNEL
   KRN_PCI_VENDORS_LIST
   KRN_FB_CONSOLE_FAILSAFE_OPT
)

message(STATUS "************** KERNEL PRIVATE OPTIONS ***************")
foreach (_opt ${kernel_private_opts_list})
   message(STATUS "${_opt} = ${${_opt}}")
endforeach()
message(STATUS "************ END KERNEL PRIVATE OPTIONS *************")

###############################################################################
# Generate kernel-private config headers from config/kernel/ templates
###############################################################################

file(GLOB _kernel_config_glob ${GLOB_CONF_DEP}
     "${PROJ_ROOT}/config/kernel/*.h")

foreach(_config_path ${_kernel_config_glob})
   get_filename_component(_config_name ${_config_path} NAME_WE)
   smart_config_file(
      ${_config_path}
      ${CMAKE_BINARY_DIR}/tilck_gen_headers/${_config_name}.h
   )
endforeach()

###############################################################################

add_subdirectory(arch)
