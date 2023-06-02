/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/mods/tracing.h>
STATIC bool simple_wildcard_match(const char *str, const char *expr);
STATIC int set_traced_syscalls_int(const char *str);

STATIC bool save_param_buffer(void *, long, char *, size_t);
STATIC bool dump_param_buffer(ulong, char *, long, long, char *, size_t);

extern bool *traced_syscalls;
extern char *traced_syscalls_str;
