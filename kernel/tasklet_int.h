/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/ringbuf.h>
#include <tilck/kernel/sync.h>

typedef void (*tasklet_func)(uptr, uptr);

typedef struct {

   tasklet_func fptr;
   tasklet_context ctx;

} tasklet;

typedef struct {

   tasklet *tasklets;
   ringbuf ringbuf;
   kcond cond;
   task_info *task;
   int priority; /* 0 => max priority */
   u32 limit;

} tasklet_thread_info;

extern tasklet_thread_info *tasklet_threads[MAX_TASKLET_THREADS];

bool run_one_tasklet(int tn);
