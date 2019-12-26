/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

enum trace_event_type {
   te_invalid = 0,
   te_sys_enter = 1,
   te_sys_exit = 2,
};

struct trace_event {

   enum trace_event_type type;
   int tid;

   u64 sys_time;

   u32 sys;
   sptr retval;
   uptr args[6];
};

void
init_tracing(void);

bool
read_trace_event(struct trace_event *e, u32 timeout_ticks);

void
trace_syscall_enter(u32 sys,
                    uptr a1, uptr a2, uptr a3, uptr a4, uptr a5, uptr a6);
void
trace_syscall_exit(u32 sys, sptr retval,
                   uptr a1, uptr a2, uptr a3, uptr a4, uptr a5, uptr a6);
