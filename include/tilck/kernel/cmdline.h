/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#define MAX_CMD_ARGS 16
#define MAX_CMD_ARG_LEN 255

enum term_serial_mode
{
   TERM_SERIAL_NONE = 0,
   TERM_SERIAL_CONSOLE = 1,

   /* --- */
   TERM_SERIAL_MODES_COUNT
};

extern const char *cmd_args[MAX_CMD_ARGS];
extern void (*self_test_to_run)(void);
extern int kopt_tty_count;
extern enum term_serial_mode kopt_serial_mode;

void parse_kernel_cmdline(const char *cmdline);
