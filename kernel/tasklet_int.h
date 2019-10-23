/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/safe_ringbuf.h>

typedef void (*tasklet_func)(uptr, uptr);

struct tasklet {

   tasklet_func fptr;
   tasklet_context ctx;
};

struct tasklet_thread {

   struct tasklet *tasklets;
   struct safe_ringbuf safe_ringbuf;
   struct task *task;
   int priority; /* 0 => max priority */
   u32 limit;
};

extern struct tasklet_thread *tasklet_threads[MAX_TASKLET_THREADS];

bool run_one_tasklet(int tn);
