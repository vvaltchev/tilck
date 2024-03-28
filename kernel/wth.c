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
#include <tilck/kernel/sort.h>

#include "wth_int.h"

int worker_threads_cnt;
struct worker_thread *worker_threads[WTH_MAX_THREADS];

u32 wth_get_queue_size(struct worker_thread *wth)
{
   return wth->rb.max_elems;
}

struct task *wth_get_task(struct worker_thread *wth)
{
   return wth->task;
}

int wth_get_priority(struct worker_thread *wth)
{
   return wth->priority;
}

const char *
wth_get_name(struct worker_thread *wth)
{
   return wth->name;
}

static long wth_cmp_func(const void *a, const void *b)
{
   const struct worker_thread *const *wa = a;
   const struct worker_thread *const *wb = b;
   return (*wa)->priority - (*wb)->priority;
}

NODISCARD bool
wth_enqueue_on(struct worker_thread *t, void (*func)(void *), void *arg)
{
   bool success, was_empty;
   ASSERT(t != NULL);

   struct wjob new_job = {
      .func = func,
      .arg = arg,
   };

   disable_preemption();

#if DEBUG_CHECKS

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

   if (get_curr_task() == t->task)
      check_in_irq_handler();

#endif

   ulong var;
   disable_interrupts(&var);
   {
      success = safe_ringbuf_write_elem(&t->rb, &new_job, &was_empty);

      if (success && was_empty && t->waiting_for_jobs) {
         wth_wakeup(t);
      }
   }
   enable_interrupts(&var);

   enable_preemption();
   return success;
}

NODISCARD bool
wth_enqueue_anywhere(int lowest_prio, void (*func)(void *), void *arg)
{
   struct worker_thread *wth;

   if (lowest_prio == 0) /* optimization for highest prio case */
      return wth_enqueue_on(worker_threads[0], func, arg);

   for (int i = worker_threads_cnt - 1; i >= 0; i--) {

      wth = worker_threads[i];

      if (wth->name)
         continue;   /* skip dedicated worker threads */

      if (wth->priority > lowest_prio)
         continue;

      if (wth_enqueue_on(wth, func, arg))
         return true; /* success */
   }

   return false;
}

struct worker_thread *
wth_find_worker(int lowest_prio)
{
   struct worker_thread *wth;

   if (lowest_prio == 0) /* optimization for highest prio case */
      return worker_threads[0];

   for (int i = worker_threads_cnt - 1; i >= 0; i--) {

      wth = worker_threads[i];

      if (wth->name)
         continue;   /* skip dedicated worker threads */

      if (wth->priority <= lowest_prio)
         return wth;
   }

   return worker_threads[0];
}

bool wth_process_single_job(struct worker_thread *t)
{
   bool success;
   struct wjob job_to_run;
   ASSERT(are_interrupts_enabled());

   disable_interrupts_forced();
   {
      success = safe_ringbuf_read_elem(&t->rb, &job_to_run);
   }
   enable_interrupts_forced();

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

   ASSERT(t != NULL);
   DEBUG_SAVE_ESP()                    /* see debug_utils.h */

   while (true) {

      DEBUG_CHECK_ESP()                /* see debug_utils.h */
      t->waiting_for_jobs = false;

      do {

         job_run = wth_process_single_job(t);

      } while (job_run);

      disable_interrupts_forced();
      {
         if (safe_ringbuf_is_empty(&t->rb)) {
            t->task->state = TASK_STATE_SLEEPING;
            t->waiting_for_jobs = true;
         }
      }
      enable_interrupts_forced();

      if (t->waiting_for_jobs) {
         kcond_signal_all(&t->completion);
         schedule();
      }
   }
}

struct task *wth_get_runnable_thread(void)
{
   ASSERT(!is_preemption_enabled());
   struct worker_thread *selected = NULL;
   ulong var;

   disable_interrupts(&var);
   for (int i = 0; i < worker_threads_cnt; i++) {

      struct worker_thread *t = worker_threads[i];

      if (t->task->state == TASK_STATE_RUNNABLE)
         if (!selected || t->priority < selected->priority)
            selected = t;
   }
   enable_interrupts(&var);

   return selected ? selected->task : NULL;
}

struct worker_thread *
wth_create_thread(const char *name, int priority, u16 queue_size)
{
   struct worker_thread *t;
   int rc, idx;

   ASSERT(!is_preemption_enabled());
   DEBUG_ONLY(check_not_in_irq_handler());

   if (worker_threads_cnt >= ARRAY_SIZE(worker_threads))
      return NULL; /* too many worker threads */

   t = kzalloc_obj(struct worker_thread);

   if (!t)
      return NULL;

   idx = worker_threads_cnt;
   t->name = name;
   t->priority = priority;
   t->jobs = kzalloc_array_obj(struct wjob, queue_size);

   if (!t->jobs) {
      kfree_obj(t, struct worker_thread);
      return NULL;
   }

   kcond_init(&t->completion);

   safe_ringbuf_init(&t->rb,
                     queue_size,
                     sizeof(struct wjob),
                     t->jobs);

   if ((rc = wth_create_thread_for(t))) {
      kfree_array_obj(t->jobs, struct wjob, queue_size);
      kfree_obj(t, struct worker_thread);
      return NULL;
   }

   worker_threads[idx] = t;

   /* Double-check that worker_threads_cnt did not change */
   ASSERT(idx == worker_threads_cnt);
   worker_threads_cnt++;

   /* Sort all the worker threads */
   insertion_sort_ptr(worker_threads, (u32)worker_threads_cnt, &wth_cmp_func);
   return t;
}

void
wth_wait_for_completion(struct worker_thread *wth)
{
   while (!wth->waiting_for_jobs)
      kcond_wait(&wth->completion, NULL, TIMER_HZ / 10);
}

static void
init_wth_create_worker_or_die(int prio, u16 queue_size)
{
   if (!wth_create_thread(NULL, prio, queue_size))
      panic("WTH: failed to create worker thread with prio %d", prio);
}

void init_worker_threads(void)
{
   worker_threads_cnt = 0;
   init_wth_create_worker_or_die(0, WTH_MAX_PRIO_QUEUE_SIZE);
}
