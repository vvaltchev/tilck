/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>

#include <signal.h>                   // system header
#include <asm-generic/signal-defs.h>  // system header

#define SIG_FL_PROCESS     (1 << 0)
#define SIG_FL_FAULT       (1 << 1)

enum sig_state {

   sig_none = 0,
   sig_pre_syscall = 1,
   sig_in_syscall = 2,
   sig_in_usermode = 3,
   sig_in_return = 4,
   sig_in_fault = 5,

} PACKED;

int send_signal_to_group(int pgid, int sig);
int send_signal_to_session(int sid, int sig);
int send_signal2(int pid, int tid, int signum, int flags);
bool process_signals(void *curr, enum sig_state new_sig_state, void *regs);
void drop_all_pending_signals(void *curr);
void reset_all_custom_signal_handlers(void *curr);

static inline int send_signal(int tid, int signum, int flags)
{
   return send_signal2(tid, tid, signum, flags);
}

#define K_SIGACTION_MASK_WORDS                              (_NSIG / NBITS)
