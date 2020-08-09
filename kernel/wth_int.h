/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/safe_ringbuf.h>
#include <tilck/kernel/sync.h>

struct wjob {
   void (*func)(void *);
   void *arg;
};

struct worker_thread {

   const char *name;
   struct wjob *jobs;
   struct safe_ringbuf rb;
   struct task *task;
   struct kcond completion;
   int priority;              /* 0 is the max priority */
   volatile bool waiting_for_jobs;
};

extern struct worker_thread *worker_threads[WTH_MAX_THREADS];

void wth_run(void *arg);
void wth_wakeup(struct worker_thread *t);
bool wth_process_single_job(struct worker_thread *t);
int wth_create_thread_for(struct worker_thread *t);
