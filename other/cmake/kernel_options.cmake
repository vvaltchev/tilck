# SPDX-License-Identifier: BSD-2-Clause
#
# Kernel-private option defaults. Included by both the kernel ExternalProject
# and the root project (for config header generation and unit tests).

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

# Derived value
if (KRN_KMALLOC_FIRST_HEAP_SIZE_KB STREQUAL "auto")

   if (KRN_TINY_KERNEL)
      set(KRN_KMALLOC_FIRST_HEAP_SIZE_KB_VAL 64)
   else()
      set(KRN_KMALLOC_FIRST_HEAP_SIZE_KB_VAL 128)
   endif()

else()
   set(KRN_KMALLOC_FIRST_HEAP_SIZE_KB_VAL ${KRN_KMALLOC_FIRST_HEAP_SIZE_KB})
endif()
