/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#define MAX_CMD_ARGS 16
#define MAX_CMD_ARG_LEN 255

extern const char *cmd_args[MAX_CMD_ARGS];
extern void (*self_test_to_run)(void);

extern long kopt_ttys;
extern bool kopt_sercon;
extern bool kopt_sched_alive_thread;
extern bool kopt_noacpi;
extern bool kopt_fb_no_opt;
extern bool kopt_fb_no_wc;
extern bool kopt_no_fpu_memcpy;
extern bool kopt_panic_kb;
extern bool kopt_big_scroll_buf;
extern bool kopt_ps2_log;
extern bool kopt_ps2_selftest;

void parse_kernel_cmdline(const char *cmdline);
