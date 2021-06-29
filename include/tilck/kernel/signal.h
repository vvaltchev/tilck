/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>

#include <signal.h>                   // system header
#include <asm-generic/signal-defs.h>  // system header

enum sig_state {

   sig_none = 0,
   sig_pre_syscall = 1,
   sig_in_syscall = 2,
   sig_in_usermode = 3,
   sig_in_return = 4,

} PACKED;

int send_signal_to_group(int pgid, int sig);
int send_signal_to_session(int sid, int sig);
int send_signal2(int pid, int tid, int signum, bool whole_process);
bool process_signals(void *curr, enum sig_state new_sig_state, void *regs);
void drop_all_pending_signals(void *curr);
void reset_all_custom_signal_handlers(void *curr);

static inline int send_signal(int tid, int signum, bool whole_process)
{
   return send_signal2(tid, tid, signum, whole_process);
}

#define K_SIGACTION_MASK_WORDS                                             2
