/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/cmdline_types.h>

#define MAX_CMD_ARGS 16
#define MAX_CMD_ARG_LEN 255

extern const char *cmd_args[MAX_CMD_ARGS];
extern void (*self_test_to_run)(void);

#define DEFINE_KOPT(name, alias, type, default) extern type kopt_##name;
   #include <tilck/common/cmdline_opts.h>
#undef DEFINE_KOPT

#define KOPT_DEFAULT_ELEM(name, alias, type, default) \
    { #name, #alias, KOPT_TYPE_##type, &kopt_##name },

void parse_kernel_cmdline(const char *cmdline);
