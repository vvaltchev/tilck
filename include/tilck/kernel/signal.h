/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/sched.h>

#include <signal.h>                   // system header
#include <asm-generic/signal-defs.h>  // system header

int send_signal2(int pid, int tid, int signum, bool whole_process);

static inline int send_signal(int tid, int signum, bool whole_process)
{
   return send_signal2(tid, tid, signum, whole_process);
}
