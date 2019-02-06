/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/sched.h>

#include <signal.h>                   // system header
#include <asm-generic/signal-defs.h>  // system header

void send_signal(task_info *ti, int signum);
