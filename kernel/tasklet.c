/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/debug_utils.h>

#include "tasklet_int.h"

STATIC u32 tasklet_threads_count;
struct worker_thread *worker_threads[MAX_WORKER_THREADS];

u32 get_worker_queue_size(u32 tn)
{
   ASSERT(tn < MAX_WORKER_THREADS);
   struct worker_thread *t = worker_threads[tn];
   return t ? t->limit : 0;
}

struct task *get_worker_thread(u32 tn)
{
   struct worker_thread *t = worker_threads[tn];

   if (!t)
      return NULL;

   ASSERT(t->task != NULL);
   return t->task;
}

static bool any_tasklets_to_run(u32 tn)
{
   struct worker_thread *t = worker_threads[tn];

   if (!t)
      return false;

   return !safe_ringbuf_is_empty(&t->rb);
}

bool enqueue_job(int tn, void (*func)(void *), void *arg)
{
   struct worker_thread *t = worker_threads[tn];
   struct task *curr = get_curr_task();
   bool success, was_empty;

   ASSERT(t != NULL);

   struct wjob new_tasklet = {
      .func = func,
      .arg = arg,
   };

   disable_preemption();

#ifdef DEBUG

   /*
    * Trying to enqueue a tasklet from the same tasklet thread can cause
    * a deadlock when the ringbuf is full if the caller waits in a loop for
    * the enqueue to succeed: the runner function won't get the control back
    * until it gets the control to execute a tasklet and, clearly, this is a
    * contradiction, leading to an endless loop. Exception: if we're running
    * in IRQ that interrupted the current task, which might be the tasklet
    * runner we'd like to enqueue in, we have to allow the enqueue to happen.
    * Simple example: a key press generates an IRQ #1 which interrupts the
    * tasklet runner #1 and wants to enqueue a tasklet there. We MUST allow
    * that to happen.
    */

   if (curr == t->task)
      check_in_irq_handler();

#endif

   success = safe_ringbuf_write_elem_ex(&t->rb, &new_tasklet, &was_empty);

   if (success && was_empty && t->waiting_for_jobs) {
      wth_wakeup(t);
   }

   enable_preemption();
   return success;
}

bool wth_process_single_job(int tn)
{
   bool success;
   struct wjob tasklet_to_run;
   struct worker_thread *t = worker_threads[tn];

   ASSERT(t != NULL);
   success = safe_ringbuf_read_elem(&t->rb, &tasklet_to_run);

   if (success) {
      /* Run the tasklet with preemption enabled */
      tasklet_to_run.func(tasklet_to_run.arg);
   }

   return success;
}

void run_worker_thread(void *arg)
{
   struct worker_thread *t = arg;
   bool tasklet_run;
   ulong var;

   ASSERT(t != NULL);
   DEBUG_SAVE_ESP()                    /* see debug_utils.h */

   while (true) {

      DEBUG_CHECK_ESP()                /* see debug_utils.h */
      t->waiting_for_jobs = false;

      do {

         tasklet_run = wth_process_single_job(t->thread_index);

      } while (tasklet_run);

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

struct task *get_runnable_worker_thread(void)
{
   ASSERT(!is_preemption_enabled());
   struct worker_thread *selected = NULL;

   for (u32 i = 0; i < tasklet_threads_count; i++) {

      struct worker_thread *t = worker_threads[i];

      if (!any_tasklets_to_run(i))
         continue;

      if (!selected || t->priority < selected->priority)
         if (t->task->state == TASK_STATE_RUNNABLE)
            selected = t;
   }

   return selected ? selected->task : NULL;
}

int create_worker_thread(int priority, u16 limit)
{
   struct worker_thread *t;
   int rc;

   ASSERT(!is_preemption_enabled());
   DEBUG_ONLY(check_not_in_irq_handler());

   if (tasklet_threads_count >= ARRAY_SIZE(worker_threads))
      return -ENFILE; /* too many tasklet runners */

   t = kzmalloc(sizeof(struct worker_thread));

   if (!t)
      return -ENOMEM;

   t->thread_index = (int)tasklet_threads_count;
   t->priority = priority;
   t->limit = limit;
   t->tasklets = kzmalloc(sizeof(struct wjob) * limit);

   if (!t->tasklets) {
      kfree2(t, sizeof(struct worker_thread));
      return -ENOMEM;
   }

   safe_ringbuf_init(&t->rb,
                     limit,
                     sizeof(struct wjob),
                     t->tasklets);

   if ((rc = wth_create_thread_for(t))) {
      kfree2(t->tasklets, sizeof(struct wjob) * limit);
      kfree2(t, sizeof(struct worker_thread));
      return rc;
   }

   worker_threads[t->thread_index] = t;

   /* Double-check that tasklet_threads_count did not change */
   ASSERT(t->thread_index == (int)tasklet_threads_count);
   tasklet_threads_count++;
   return t->thread_index;
}

void init_worker_threads(void)
{
   int tn;

   tasklet_threads_count = 0;
   tn = create_worker_thread(0 /* priority */, MAX_PRIO_TASKLET_QUEUE_SIZE);

   if (tn < 0)
      panic("init_tasklet_thread() failed");

   ASSERT(tn == 0);
}
