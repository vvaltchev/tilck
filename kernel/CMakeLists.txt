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

###############################################################################
# Kernel-private options (not exported to other subprojects)
###############################################################################

# Non-boolean kernel options
set(KRN_TIMER_HZ            250 CACHE STRING "System timer HZ")
set(KRN_USER_STACK_PAGES     16 CACHE STRING "User apps stack size in pages")
set(KRN_MAX_HANDLES          16 CACHE STRING "Max handles/process (keep small)")

set(KRN_FBCON_BIGFONT_THR   160 CACHE STRING
    "Max term cols with 8x16 font. After that, a 16x32 font will be used")

set(KRN_TERM_SCROLL_LINES 5 CACHE STRING
    "Number of lines to scroll on Shift+PgUp/PgDown")

set(KRN_KMALLOC_FIRST_HEAP_SIZE_KB "auto" CACHE STRING
    "Size in KB of kmalloc's first heap. Must be multiple of 64.")

# Boolean kernel options (enabled by default)

set(KRN_TRACK_NESTED_INTERR ON CACHE BOOL
    "Track the nested interrupts")

set(KRN_STACK_ISOLATION ON CACHE BOOL
    "Put the kernel stack in hi the vmem in isolated pages")

set(KRN_FB_CONSOLE_BANNER ON CACHE BOOL
    "Show a top banner when using fb_console")

set(KRN_FB_CONSOLE_CURSOR_BLINK ON CACHE BOOL
    "Support cursor blinking in the fb_console")

if ($ENV{TILCK_NO_LOGO})
   set(KRN_SHOW_LOGO OFF CACHE BOOL
      "Show Tilck's logo after boot")
else()
   set(KRN_SHOW_LOGO ON CACHE BOOL
      "Show Tilck's logo after boot")
endif()

set(KRN_SYMBOLS ON CACHE BOOL
    "Keep symbol tables loaded in the kernel for backtraces and self tests")

set(KRN_PRINTK_ON_CURR_TTY ON CACHE BOOL
    "Make printk() always flush on the current TTY")

set(KRN_CLOCK_DRIFT_COMP ON CACHE BOOL
    "Compensate periodically for the clock drift in the system time")

set(KRN_TRACE_PRINTK_ON_BOOT ON CACHE BOOL
    "Make trace_printk() to be always enabled since boot time")

# Boolean kernel options (disabled by default)

set(KRN_PAGE_FAULT_PRINTK OFF CACHE BOOL
    "Use printk() to display info when a process is killed due to page fault")

set(KRN_NO_SYS_WARN OFF CACHE BOOL
    "Show a warning when a not-implemented syscall is called")

set(KRN_BIG_IO_BUF OFF CACHE BOOL "Use a much-bigger buffer for I/O")

set(KRN_KMALLOC_HEAVY_STATS OFF CACHE BOOL
    "Count the number of allocations for each distinct size")

set(KRN_KMALLOC_FREE_MEM_POISONING OFF CACHE BOOL
    "Make kfree() to poison the memory")

set(KRN_KMALLOC_SUPPORT_DEBUG_LOG OFF CACHE BOOL
    "Compile-in kmalloc debug messages")

set(KRN_KMALLOC_SUPPORT_LEAK_DETECTOR OFF CACHE BOOL
    "Compile-in kmalloc's leak detector")

set(KRN_FB_CONSOLE_USE_ALT_FONTS OFF CACHE BOOL
    "Use the fonts in other/alt_fonts instead of the default ones")

set(KRN_RESCHED_ENABLE_PREEMPT OFF CACHE BOOL
    "Check for need_resched and yield in enable_preemption()")

set(KRN_MINIMAL_TIME_SLICE OFF CACHE BOOL
    "Make the time slice to be 1 tick in order to trigger more race conditions")

set(KRN_TINY_KERNEL OFF CACHE BOOL "\
Advanced option, use carefully. Forces the Tilck kernel \
to be as small as possible. Incompatibile with many modules \
like console, fb, tracing and several kernel options like \
KERNEL_SELFTESTS")

set(KRN_PCI_VENDORS_LIST OFF CACHE BOOL
    "Compile-in the list of all known PCI vendors")

set(KRN_FB_CONSOLE_FAILSAFE_OPT OFF CACHE BOOL
    "Optimize fb_console's failsafe mode for older machines")

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

if (KRN_KMALLOC_FIRST_HEAP_SIZE_KB STREQUAL "auto")

   if (KRN_TINY_KERNEL)
      set(KRN_KMALLOC_FIRST_HEAP_SIZE_KB_VAL 64)
   else()
      set(KRN_KMALLOC_FIRST_HEAP_SIZE_KB_VAL 128)
   endif()

else()
   set(KRN_KMALLOC_FIRST_HEAP_SIZE_KB_VAL ${KRN_KMALLOC_FIRST_HEAP_SIZE_KB})
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
