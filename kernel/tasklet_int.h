/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/safe_ringbuf.h>

struct tasklet {
   void (*func)(void *);
   void *arg;
};

struct tasklet_thread {

   struct tasklet *tasklets;
   struct safe_ringbuf rb;
   struct task *task;
   int thread_index;          /* index of this obj in tasklet_threads */
   int priority;              /* 0 is the max priority */
   u32 limit;
   bool waiting_for_jobs;
};

extern struct tasklet_thread *tasklet_threads[MAX_TASKLET_THREADS];

bool run_one_tasklet(int tn);
int tasklet_create_thread_for(struct tasklet_thread *t);
void tasklet_wakeup_runner(struct tasklet_thread *t);
