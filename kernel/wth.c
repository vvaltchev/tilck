/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/worker_thread.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/debug_utils.h>

#include "wth_int.h"

STATIC u32 worker_threads_cnt;
struct worker_thread *worker_threads[WTH_MAX_THREADS];

u32 wth_get_queue_size(int wth)
{
   ASSERT(wth < WTH_MAX_THREADS);
   struct worker_thread *t = worker_threads[wth];
   return t ? t->rb.max_elems : 0;
}

struct task *wth_get_task(int wth)
{
   struct worker_thread *t = worker_threads[wth];

   if (!t)
      return NULL;

   ASSERT(t->task != NULL);
   return t->task;
}

static bool any_jobs_to_run(u32 wth)
{
   struct worker_thread *t = worker_threads[wth];

   if (!t)
      return false;

   return !safe_ringbuf_is_empty(&t->rb);
}

bool wth_enqueue_job(int wth, void (*func)(void *), void *arg)
{
   struct worker_thread *t = worker_threads[wth];
   struct task *curr = get_curr_task();
   bool success, was_empty;

   ASSERT(t != NULL);

   struct wjob new_job = {
      .func = func,
      .arg = arg,
   };

   disable_preemption();

#ifdef DEBUG

   /*
    * Trying to enqueue a job from the same job thread can cause a deadlock when
    * the ringbuf is full if the caller waits in a loop for the enqueue to
    * succeed: the runner function won't get the control back until it gets the
    * control to execute a job and, clearly, this is a contradiction, leading to
    * an endless loop. Exception: if we're running in IRQ that interrupted the
    * current task, which might be the job runner we'd like to enqueue in, we
    * have to allow the enqueue to happen. Simple example: a key press generates
    * an IRQ #1 which interrupts the worker thread #1 and wants to enqueue a job
    * there. We MUST allow that to happen.
    */

   if (curr == t->task)
      check_in_irq_handler();

#endif

   success = safe_ringbuf_write_elem_ex(&t->rb, &new_job, &was_empty);

   if (success && was_empty && t->waiting_for_jobs) {
      wth_wakeup(t);
   }

   enable_preemption();
   return success;
}

bool wth_process_single_job(int wth)
{
   bool success;
   struct wjob job_to_run;
   struct worker_thread *t = worker_threads[wth];

   ASSERT(t != NULL);
   success = safe_ringbuf_read_elem(&t->rb, &job_to_run);

   if (success) {
      /* Run the job with preemption enabled */
      job_to_run.func(job_to_run.arg);
   }

   return success;
}

void wth_run(void *arg)
{
   struct worker_thread *t = arg;
   bool job_run;
   ulong var;

   ASSERT(t != NULL);
   DEBUG_SAVE_ESP()                    /* see debug_utils.h */

   while (true) {

      DEBUG_CHECK_ESP()                /* see debug_utils.h */
      t->waiting_for_jobs = false;

      do {

         job_run = wth_process_single_job(t->thread_index);

      } while (job_run);

      disable_interrupts(&var);
      {
         if (safe_ringbuf_is_empty(&t->rb)) {
            t->task->state = TASK_STATE_SLEEPING;
            t->waiting_for_jobs = true;
         }
      }
      enable_interrupts(&var);

      if (t->waiting_for_jobs)
         kernel_yield();
   }
}

struct task *wth_get_runnable_thread(void)
{
   ASSERT(!is_preemption_enabled());
   struct worker_thread *selected = NULL;

   for (u32 i = 0; i < worker_threads_cnt; i++) {

      struct worker_thread *t = worker_threads[i];

      if (!any_jobs_to_run(i))
         continue;

      if (!selected || t->priority < selected->priority)
         if (t->task->state == TASK_STATE_RUNNABLE)
            selected = t;
   }

   return selected ? selected->task : NULL;
}

int wth_create_thread(int priority, u16 queue_size)
{
   struct worker_thread *t;
   int rc;

   ASSERT(!is_preemption_enabled());
   DEBUG_ONLY(check_not_in_irq_handler());

   if (worker_threads_cnt >= ARRAY_SIZE(worker_threads))
      return -ENFILE; /* too many worker threads */

   t = kzmalloc(sizeof(struct worker_thread));

   if (!t)
      return -ENOMEM;

   t->thread_index = (int)worker_threads_cnt;
   t->priority = priority;
   t->jobs = kzmalloc(sizeof(struct wjob) * queue_size);

   if (!t->jobs) {
      kfree2(t, sizeof(struct worker_thread));
      return -ENOMEM;
   }

   safe_ringbuf_init(&t->rb,
                     queue_size,
                     sizeof(struct wjob),
                     t->jobs);

   if ((rc = wth_create_thread_for(t))) {
      kfree2(t->jobs, sizeof(struct wjob) * queue_size);
      kfree2(t, sizeof(struct worker_thread));
      return rc;
   }

   worker_threads[t->thread_index] = t;

   /* Double-check that worker_threads_cnt did not change */
   ASSERT(t->thread_index == (int)worker_threads_cnt);
   worker_threads_cnt++;
   return t->thread_index;
}

void init_worker_threads(void)
{
   int wth;

   worker_threads_cnt = 0;
   wth = wth_create_thread(0 /* priority */, WTH_MAX_PRIO_QUEUE_SIZE);

   if (wth < 0)
      panic("init_worker_threads() failed");

   ASSERT(wth == 0);
}
