/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#define MAX_CMD_ARGS 16
#define MAX_CMD_ARG_LEN 255


enum kopt_type {
   KOPT_TYPE_bool,
   KOPT_TYPE_long,
   KOPT_TYPE_ulong,
   KOPT_TYPE_wordstr,
};

struct kopt {
   const char *name;
   const char *alias;
   enum kopt_type type;
   void *data;
};

typedef const char *wordstr;

extern const char *cmd_args[MAX_CMD_ARGS];
extern void (*self_test_to_run)(void);

#define DEFINE_KOPT(name, alias, type, default) extern type kopt_##name;
   #include <tilck/common/cmdline_opts.h>
#undef DEFINE_KOPT

void parse_kernel_cmdline(const char *cmdline);
