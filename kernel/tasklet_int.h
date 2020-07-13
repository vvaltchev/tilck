/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/safe_ringbuf.h>

struct wjob {
   void (*func)(void *);
   void *arg;
};

struct worker_thread {

   struct wjob *tasklets;
   struct safe_ringbuf rb;
   struct task *task;
   int thread_index;          /* index of this obj in worker_threads */
   int priority;              /* 0 is the max priority */
   u32 limit;
   bool waiting_for_jobs;
};

extern struct worker_thread *worker_threads[MAX_WORKER_THREADS];

bool wth_process_single_job(int tn);
int wth_create_thread_for(struct worker_thread *t);
void wth_wakeup(struct worker_thread *t);
