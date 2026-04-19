# SPDX-License-Identifier: BSD-2-Clause
#
# Kernel-private option defaults. Included from the root CMakeLists.txt
# (so tilck_option() emits to the shared sidecar) AND from the kernel
# and tests/unit ExternalProjects (which re-use the same CACHE entries
# via set(... CACHE ...) idempotency + tilck_option()'s GLOBAL emission
# guard).

###############################################################################
# Kernel-private options (not exported to other subprojects)
###############################################################################

# --- Numeric / enum options ---

tilck_option(KRN_TIMER_HZ
   TYPE     INT
   CATEGORY "Kernel/Timer"
   DEFAULT  250
   HELP     "System timer HZ"
)

tilck_option(KRN_USER_STACK_PAGES
   TYPE     UINT
   CATEGORY "Kernel/Memory"
   DEFAULT  16
   HELP     "User apps stack size in pages"
)

tilck_option(KRN_MAX_HANDLES
   TYPE     UINT
   CATEGORY "Kernel/Limits"
   DEFAULT  16
   HELP     "Max handles/process (keep small)"
)

tilck_option(KRN_FBCON_BIGFONT_THR
   TYPE     UINT
   CATEGORY "Kernel/Terminal"
   DEFAULT  160
   HELP     "Max term cols with 8x16 font. After that, a 16x32 font"
            "will be used."
)

tilck_option(KRN_TERM_SCROLL_LINES
   TYPE     UINT
   CATEGORY "Kernel/Terminal"
   DEFAULT  5
   HELP     "Number of lines to scroll on Shift+PgUp/PgDown"
)

tilck_option(KRN_KMALLOC_FIRST_HEAP_SIZE_KB
   TYPE     ENUM
   CATEGORY "Kernel/Memory"
   DEFAULT  "auto"
   STRINGS  auto 64 128 256 512
   HELP     "Size in KB of kmalloc's first heap. Must be a multiple of"
            "64. 'auto' picks 64 for tiny kernels, 128 otherwise."
)

# --- Boolean kernel options (enabled by default) ---

tilck_option(KRN_TRACK_NESTED_INTERR
   TYPE     BOOL
   CATEGORY "Kernel/Debug"
   DEFAULT  ON
   HELP     "Track the nested interrupts"
)

tilck_option(KRN_STACK_ISOLATION
   TYPE     BOOL
   CATEGORY "Kernel/Security"
   DEFAULT  ON
   HELP     "Put the kernel stack in hi the vmem in isolated pages"
)

tilck_option(KRN_FB_CONSOLE_BANNER
   TYPE     BOOL
   CATEGORY "Kernel/Terminal"
   DEFAULT  ON
   HELP     "Show a top banner when using fb_console"
)

tilck_option(KRN_FB_CONSOLE_CURSOR_BLINK
   TYPE     BOOL
   CATEGORY "Kernel/Terminal"
   DEFAULT  ON
   HELP     "Support cursor blinking in the fb_console"
)

# TILCK_NO_LOGO is an out-of-band env-var shortcut to flip the default
# OFF. Preserved here — tilck_option's own $ENV{NAME} override still
# works as a second path (set TILCK_NO_LOGO=1, or KRN_SHOW_LOGO=OFF).
if ($ENV{TILCK_NO_LOGO})
   set(_krn_show_logo_default OFF)
else()
   set(_krn_show_logo_default ON)
endif()
tilck_option(KRN_SHOW_LOGO
   TYPE     BOOL
   CATEGORY "Kernel/Appearance"
   DEFAULT  ${_krn_show_logo_default}
   HELP     "Show Tilck's logo after boot"
)
unset(_krn_show_logo_default)

tilck_option(KRN_SYMBOLS
   TYPE     BOOL
   CATEGORY "Kernel/Debug"
   DEFAULT  ON
   HELP     "Keep symbol tables loaded in the kernel for backtraces"
            "and self tests"
)

tilck_option(KRN_PRINTK_ON_CURR_TTY
   TYPE     BOOL
   CATEGORY "Kernel/Terminal"
   DEFAULT  ON
   HELP     "Make printk() always flush on the current TTY"
)

tilck_option(KRN_CLOCK_DRIFT_COMP
   TYPE     BOOL
   CATEGORY "Kernel/Timer"
   DEFAULT  ON
   HELP     "Compensate periodically for the clock drift in the"
            "system time"
)

tilck_option(KRN_TRACE_PRINTK_ON_BOOT
   TYPE     BOOL
   CATEGORY "Kernel/Debug"
   DEFAULT  ON
   HELP     "Make trace_printk() to be always enabled since boot time"
)

# --- Boolean kernel options (disabled by default) ---

tilck_option(KRN_PAGE_FAULT_PRINTK
   TYPE     BOOL
   CATEGORY "Kernel/Debug"
   DEFAULT  OFF
   HELP     "Use printk() to display info when a process is killed"
            "due to page fault"
)

tilck_option(KRN_NO_SYS_WARN
   TYPE     BOOL
   CATEGORY "Kernel/Debug"
   DEFAULT  OFF
   HELP     "Show a warning when a not-implemented syscall is called"
)

tilck_option(KRN_BIG_IO_BUF
   TYPE     BOOL
   CATEGORY "Kernel/Memory"
   DEFAULT  OFF
   HELP     "Use a much-bigger buffer for I/O"
)

tilck_option(KRN_KMALLOC_HEAVY_STATS
   TYPE     BOOL
   CATEGORY "Kernel/Memory"
   DEFAULT  OFF
   HELP     "Count the number of allocations for each distinct size"
)

tilck_option(KRN_KMALLOC_FREE_MEM_POISONING
   TYPE     BOOL
   CATEGORY "Kernel/Memory"
   DEFAULT  OFF
   HELP     "Make kfree() to poison the memory"
)

tilck_option(KRN_KMALLOC_SUPPORT_DEBUG_LOG
   TYPE     BOOL
   CATEGORY "Kernel/Memory"
   DEFAULT  OFF
   HELP     "Compile-in kmalloc debug messages"
)

tilck_option(KRN_KMALLOC_SUPPORT_LEAK_DETECTOR
   TYPE     BOOL
   CATEGORY "Kernel/Memory"
   DEFAULT  OFF
   HELP     "Compile-in kmalloc's leak detector"
)

tilck_option(KRN_FB_CONSOLE_USE_ALT_FONTS
   TYPE     BOOL
   CATEGORY "Kernel/Terminal"
   DEFAULT  OFF
   HELP     "Use the fonts in other/alt_fonts instead of the default"
            "ones"
)

tilck_option(KRN_RESCHED_ENABLE_PREEMPT
   TYPE     BOOL
   CATEGORY "Kernel/Scheduler"
   DEFAULT  OFF
   HELP     "Check for need_resched and yield in enable_preemption()"
)

tilck_option(KRN_MINIMAL_TIME_SLICE
   TYPE     BOOL
   CATEGORY "Kernel/Scheduler"
   DEFAULT  OFF
   HELP     "Make the time slice to be 1 tick in order to trigger"
            "more race conditions"
)

tilck_option(KRN_TINY_KERNEL
   TYPE     BOOL
   CATEGORY "Kernel/Advanced"
   DEFAULT  OFF
   HELP     "Advanced option, use carefully. Forces the Tilck kernel"
            "to be as small as possible. Incompatibile with many"
            "modules like console, fb, tracing and several kernel"
            "options like KERNEL_SELFTESTS."
)

tilck_option(KRN_PCI_VENDORS_LIST
   TYPE     BOOL
   CATEGORY "Kernel/Debug"
   DEFAULT  OFF
   HELP     "Compile-in the list of all known PCI vendors"
)

tilck_option(KRN_FB_CONSOLE_FAILSAFE_OPT
   TYPE     BOOL
   CATEGORY "Kernel/Terminal"
   DEFAULT  OFF
   HELP     "Optimize fb_console's failsafe mode for older machines"
)

# --- Derived value (runs after all options are defined) ---

if (KRN_KMALLOC_FIRST_HEAP_SIZE_KB STREQUAL "auto")

   if (KRN_TINY_KERNEL)
      set(KRN_KMALLOC_FIRST_HEAP_SIZE_KB_VAL 64)
   else()
      set(KRN_KMALLOC_FIRST_HEAP_SIZE_KB_VAL 128)
   endif()

else()
   set(KRN_KMALLOC_FIRST_HEAP_SIZE_KB_VAL ${KRN_KMALLOC_FIRST_HEAP_SIZE_KB})
endif()
