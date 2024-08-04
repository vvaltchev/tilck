/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck_gen_headers/config_debug.h>
#include <tilck_gen_headers/config_boot.h>
#include <tilck_gen_headers/config_kmalloc.h>
#include <tilck_gen_headers/config_userlim.h>
#include <tilck_gen_headers/config_mm.h>
#include <tilck_gen_headers/config_sched.h>
#include <tilck_gen_headers/mod_console.h>
#include <tilck_gen_headers/mod_fb.h>

#include <tilck/mods/sysfs.h>
#include <tilck/mods/sysfs_utils.h>
#include <tilck/common/build_info.h>

/* config */
DEF_STATIC_CONF_RO(STRING, buildtype,              BUILDTYPE_STR);
DEF_STATIC_CONF_RO(STRING, version,                tilck_build_info.ver);
DEF_STATIC_CONF_RO(STRING, arch,                   tilck_build_info.arch);
DEF_STATIC_CONF_RO(STRING, commit,                 tilck_build_info.commit);
DEF_STATIC_CONF_RO(BOOL,   debugchecks,            DEBUG_CHECKS);

/* kernel */
DEF_STATIC_CONF_RO(ULONG, timer_hz,                TIMER_HZ);
DEF_STATIC_CONF_RO(ULONG, time_slice_ticks,        TIME_SLICE_TICKS);
DEF_STATIC_CONF_RO(ULONG, stack_pages,             KERNEL_STACK_PAGES);
DEF_STATIC_CONF_RO(ULONG, user_stack_pages,        USER_STACK_PAGES);
DEF_STATIC_CONF_RO(BOOL,  track_nested_int,        KRN_TRACK_NESTED_INTERR);
DEF_STATIC_CONF_RO(BOOL,  panic_backtrace,         PANIC_SHOW_STACKTRACE);
DEF_STATIC_CONF_RO(BOOL,  panic_regs,              PANIC_SHOW_REGS);
DEF_STATIC_CONF_RO(BOOL,  selftests,               KERNEL_SELFTESTS);
DEF_STATIC_CONF_RO(BOOL,  stack_isolation,         KERNEL_STACK_ISOLATION);
DEF_STATIC_CONF_RO(BOOL,  symbols,                 KERNEL_SYMBOLS);
DEF_STATIC_CONF_RO(BOOL,  printk_on_curr_tty,      KRN_PRINTK_ON_CURR_TTY);
DEF_STATIC_CONF_RO(BOOL,  resched_enable_preempt,  KRN_RESCHED_ENABLE_PREEMPT);
DEF_STATIC_CONF_RO(BOOL,  big_io_buf,              KERNEL_BIG_IO_BUF);
DEF_STATIC_CONF_RO(BOOL,  gcov,                    KERNEL_GCOV);
DEF_STATIC_CONF_RO(BOOL,  fork_no_cow,             FORK_NO_COW);
DEF_STATIC_CONF_RO(BOOL,  mmap_no_cow,             MMAP_NO_COW);
DEF_STATIC_CONF_RO(BOOL,  ubsan,                   KERNEL_UBSAN);
DEF_STATIC_CONF_RO(BOOL,  kernel_64bit_offt,       KERNEL_64BIT_OFFT);
DEF_STATIC_CONF_RO(BOOL,  clock_drift_comp,        KRN_CLOCK_DRIFT_COMP);

/* console */
DEF_STATIC_CONF_RO(ULONG, big_font_threshold,      FBCON_BIGFONT_THR);
DEF_STATIC_CONF_RO(BOOL,  banner,                  FB_CONSOLE_BANNER);
DEF_STATIC_CONF_RO(BOOL,  cursor_blink,            FB_CONSOLE_CURSOR_BLINK);
DEF_STATIC_CONF_RO(BOOL,  use_alt_fonts,           FB_CONSOLE_USE_ALT_FONTS);
DEF_STATIC_CONF_RO(BOOL,  show_logo,               KERNEL_SHOW_LOGO);
DEF_STATIC_CONF_RO(BOOL,  big_scroll_buf,          TERM_BIG_SCROLL_BUF);
DEF_STATIC_CONF_RO(BOOL,  failsafe_opt,            FB_CONSOLE_FAILSAFE_OPT);
