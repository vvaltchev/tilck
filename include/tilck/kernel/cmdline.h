/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#define MAX_CMD_ARGS 16
#define MAX_CMD_ARG_LEN 255

extern const char *cmd_args[MAX_CMD_ARGS];
extern void (*self_test_to_run)(void);

extern int kopt_tty_count;
extern bool kopt_serial_console;
extern bool kopt_sched_alive_thread;

void parse_kernel_cmdline(const char *cmdline);
