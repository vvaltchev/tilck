/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Special header without protections like '#pragma once', because it want
 * to include it multiple times, re-defining DEFINE_KOPT() each time.
 *
 * All the Tilck kernel boot options. Each option to the cmdline at boot
 * using the format "-<name> [param]" or "-<alias> [param]".
 */


/*          name              ,alias, type,    default            */
DEFINE_KOPT(ttys              ,     , long,    TTY_COUNT)
DEFINE_KOPT(selftest          ,     , wordstr, NULL)

DEFINE_KOPT(sched_alive_thread, sat , bool,    false)
DEFINE_KOPT(sercon            ,     , bool,    !MOD_console)
DEFINE_KOPT(noacpi            ,     , bool,    false)
DEFINE_KOPT(fb_no_opt         ,     , bool,    false)
DEFINE_KOPT(fb_no_wc          ,     , bool,    false)
DEFINE_KOPT(no_fpu_memcpy     ,     , bool,    false)
DEFINE_KOPT(panic_kb          , pk  , bool,    false)
DEFINE_KOPT(panic_nobt        , nobt, bool,    !PANIC_SHOW_STACKTRACE)
DEFINE_KOPT(panic_regs        , pr  , bool,    PANIC_SHOW_REGS)
DEFINE_KOPT(panic_mmap        , pm  , bool,    false)
DEFINE_KOPT(big_scroll_buf    , bb  , bool,    TERM_BIG_SCROLL_BUF)
DEFINE_KOPT(ps2_log           , plg , bool,    PS2_VERBOSE_DEBUG_LOG)
DEFINE_KOPT(ps2_selftest      , pse , bool,    PS2_DO_SELFTEST)
