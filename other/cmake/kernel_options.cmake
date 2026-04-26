# SPDX-License-Identifier: BSD-2-Clause
#
# Kernel-private option defaults. Included from the root CMakeLists.txt
# (so tilck_option() emits to the shared sidecar) AND from the kernel
# and tests/unit ExternalProjects (which re-use the same CACHE entries
# via set(... CACHE ...) idempotency + tilck_option()'s GLOBAL emission
# guard).
#
# Conventions for HELP:
#   - First HELP line is the short prompt shown in mconf's menu. mconf
#     truncates item text to roughly menu_width - 6 chars, which works
#     out to ~60-65 chars on an 80-col terminal. Keep it self-
#     contained (a complete phrase) and <= ~55 chars to display
#     cleanly even with indent.
#   - Remaining HELP lines form the help body shown when the user
#     presses '?' or F1. lxdialog auto-wraps each paragraph; break
#     on semantic boundaries rather than column width.

###############################################################################
# Kernel-private options (not exported to other subprojects)
###############################################################################

# =====================================================================
# Kernel / Memory
# =====================================================================

tilck_option(KRN_USER_STACK_PAGES
   TYPE     UINT
   CATEGORY "Kernel Memory"
   DEFAULT  16
   HELP     "User-process stack size (pages)"
)

# Conditional sub-option pattern: a BOOL toggle controls the
# visibility of a follow-on numeric option in mconf. When the toggle
# is OFF, the sub-option is hidden and the derived value below
# computes an "auto" heap size based on KRN_TINY_KERNEL. When the
# toggle is ON, mconf exposes the KB input and the explicit value
# wins. CMake only needs to know about the two cache vars and the
# derived fallback; the hide/show is a Kconfig-level effect.
tilck_option(KRN_KMALLOC_CUSTOM_FIRST_HEAP
   TYPE     BOOL
   CATEGORY "Kernel Memory"
   DEFAULT  OFF
   HELP     "Custom first kmalloc heap size"
            "When OFF (the default), kmalloc's first heap is sized"
            "automatically: 64 KB if KRN_TINY_KERNEL is ON, 128 KB"
            "otherwise. Turn ON to set an explicit size via the"
            "sub-option that appears below."
)

tilck_option(KRN_KMALLOC_FIRST_HEAP_SIZE_KB
   TYPE     UINT
   CATEGORY "Kernel Memory"
   DEFAULT  128
   DEPENDS  KRN_KMALLOC_CUSTOM_FIRST_HEAP
   HELP     "First kmalloc heap size (KB)"
            "Must be a multiple of 64. Only used when"
            "KRN_KMALLOC_CUSTOM_FIRST_HEAP is ON; otherwise the"
            "derived 'auto' value applies."
)

tilck_option(KRN_BIG_IO_BUF
   TYPE     BOOL
   CATEGORY "Kernel Memory"
   DEFAULT  OFF
   HELP     "Use a larger buffer for I/O"
)

tilck_option(KRN_KMALLOC_HEAVY_STATS
   TYPE     BOOL
   CATEGORY "Kernel Memory"
   DEFAULT  OFF
   HELP     "Count allocations per distinct size (diagnostic)"
)

tilck_option(KRN_KMALLOC_FREE_MEM_POISONING
   TYPE     BOOL
   CATEGORY "Kernel Memory"
   DEFAULT  OFF
   HELP     "Poison freed memory in kfree()"
)

tilck_option(KRN_KMALLOC_SUPPORT_DEBUG_LOG
   TYPE     BOOL
   CATEGORY "Kernel Memory"
   DEFAULT  OFF
   HELP     "Compile-in kmalloc debug messages"
)

tilck_option(KRN_KMALLOC_SUPPORT_LEAK_DETECTOR
   TYPE     BOOL
   CATEGORY "Kernel Memory"
   DEFAULT  OFF
   HELP     "Compile-in kmalloc's leak detector"
)

# =====================================================================
# Kernel / Terminal
# =====================================================================

tilck_option(KRN_TERM_SCROLL_LINES
   TYPE     UINT
   CATEGORY "Kernel Terminal"
   DEFAULT  5
   HELP     "Lines to scroll on Shift+PgUp / Shift+PgDown"
)

tilck_option(KRN_PRINTK_ON_CURR_TTY
   TYPE     BOOL
   CATEGORY "Kernel Terminal"
   DEFAULT  ON
   HELP     "Always flush printk() on the current TTY"
)

# KRN_FB_* options (fb_console banner / cursor / fonts / big-font
# threshold / failsafe) live under Modules/fb now, declared in
# modules/fb/options.cmake. They DEPEND on MOD_fb so mconf hides
# them when the fb module is disabled.

# =====================================================================
# Kernel / Debug
# =====================================================================

tilck_option(KRN_TRACK_NESTED_INTERR
   TYPE     BOOL
   CATEGORY "Kernel Debug"
   DEFAULT  ON
   HELP     "Track nested interrupts"
)

tilck_option(KRN_SYMBOLS
   TYPE     BOOL
   CATEGORY "Kernel Debug"
   DEFAULT  ON
   HELP     "Keep kernel symbol tables in the image"
            "Needed for readable backtraces and kernel self-tests."
)

tilck_option(KRN_TRACE_PRINTK_ON_BOOT
   TYPE     BOOL
   CATEGORY "Kernel Debug"
   DEFAULT  ON
   HELP     "Enable trace_printk() from boot time"
)

tilck_option(KRN_PAGE_FAULT_PRINTK
   TYPE     BOOL
   CATEGORY "Kernel Debug"
   DEFAULT  OFF
   HELP     "Print info when killing a process on page fault"
)

tilck_option(KRN_NO_SYS_WARN
   TYPE     BOOL
   CATEGORY "Kernel Debug"
   DEFAULT  OFF
   HELP     "Warn on calls to not-implemented syscalls"
)

# KRN_PCI_VENDORS_LIST moved to modules/pci/options.cmake (DEPENDS
# MOD_pci) — only meaningful when the pci module is compiled-in.

# =====================================================================
# Kernel / Misc
#
# Consolidates categories that would otherwise hold only one or two
# options each (timer, limits, scheduler, security, appearance,
# advanced). Nested menus per option aren't useful.
# =====================================================================

tilck_option(KRN_TIMER_HZ
   TYPE     INT
   CATEGORY "Kernel Misc"
   DEFAULT  250
   HELP     "Kernel timer frequency in Hz"
)

tilck_option(KRN_CLOCK_DRIFT_COMP
   TYPE     BOOL
   CATEGORY "Kernel Misc"
   DEFAULT  ON
   HELP     "Periodically compensate for clock drift"
)

tilck_option(KRN_MAX_HANDLES
   TYPE     UINT
   CATEGORY "Kernel Misc"
   DEFAULT  16
   HELP     "Max open handles per process (keep small)"
)

tilck_option(KRN_RESCHED_ENABLE_PREEMPT
   TYPE     BOOL
   CATEGORY "Kernel Misc"
   DEFAULT  OFF
   HELP     "Check need_resched in enable_preemption()"
)

tilck_option(KRN_MINIMAL_TIME_SLICE
   TYPE     BOOL
   CATEGORY "Kernel Misc"
   DEFAULT  OFF
   HELP     "Use 1-tick time slice (stress test)"
            "Forces the scheduler's time slice to a single tick to"
            "trigger more race conditions in testing. Not meant for"
            "production builds."
)

tilck_option(KRN_HANG_DETECTION
   TYPE     BOOL
   CATEGORY "Kernel Misc"
   DEFAULT  OFF
   HELP     "Compile in the hang detector and per-task state dumper"
            "Adds a forward-progress watchdog inside the optional"
            "sched-alive thread (see -sat / KERNEL_SAT). When the"
            "scheduler stops doing context switches for an interval,"
            "the watchdog dumps every task's state, the wait object"
            "each one is parked on, the open pipe fds, and a per-pipe"
            "ring of recent dup/close events. Useful for diagnosing"
            "stress-test hangs (lost wakeups, missed broadcasts,"
            "scheduler wedges); off by default because of the small"
            "but always-on per-pipe accounting cost."
)

tilck_option(KRN_STACK_ISOLATION
   TYPE     BOOL
   CATEGORY "Kernel Misc"
   DEFAULT  ON
   HELP     "Isolate the kernel stack in high virtual memory"
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
   CATEGORY "Kernel Misc"
   DEFAULT  ${_krn_show_logo_default}
   HELP     "Show Tilck's logo at boot"
)
unset(_krn_show_logo_default)

tilck_option(KRN_TINY_KERNEL
   TYPE     BOOL
   CATEGORY "Kernel Misc"
   DEFAULT  OFF
   HELP     "Force minimal kernel size (advanced)"
            "Makes the Tilck kernel as small as possible. Incompatible"
            "with many modules (console, fb, tracing, ...) and several"
            "kernel options like KERNEL_SELFTESTS. Use carefully."
)

# =====================================================================
# Derived value (runs after all options are defined)
# =====================================================================

if (KRN_KMALLOC_CUSTOM_FIRST_HEAP)
   # Toggle ON: user set an explicit size via the menu / -D.
   set(KRN_KMALLOC_FIRST_HEAP_SIZE_KB_VAL ${KRN_KMALLOC_FIRST_HEAP_SIZE_KB})
elseif (KRN_TINY_KERNEL)
   set(KRN_KMALLOC_FIRST_HEAP_SIZE_KB_VAL 64)
else()
   set(KRN_KMALLOC_FIRST_HEAP_SIZE_KB_VAL 128)
endif()
