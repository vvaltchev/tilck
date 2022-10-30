/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/cmdline.h>

struct arg_parse_ctx;

STATIC void
use_kernel_arg(struct arg_parse_ctx *ctx, int arg_num, const char *arg);
