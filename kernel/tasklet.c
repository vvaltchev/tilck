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

#include "tasklet_int.h"

STATIC u32 tasklet_threads_count;
struct tasklet_thread *tasklet_threads[MAX_TASKLET_THREADS];

u32 get_tasklet_runner_limit(u32 tn)
{
   ASSERT(tn < MAX_TASKLET_THREADS);
   struct tasklet_thread *t = tasklet_threads[tn];
   return t ? t->limit : 0;
}

struct task *get_tasklet_runner(u32 tn)
{
   struct tasklet_thread *t = tasklet_threads[tn];

   if (!t)
      return NULL;

   ASSERT(t->task != NULL);
   return t->task;
}

bool any_tasklets_to_run(u32 tn)
{
   struct tasklet_thread *t = tasklet_threads[tn];

   if (!t)
      return false;

   return !safe_ringbuf_is_empty(&t->rb);
}

bool enqueue_tasklet_int(int tn, void *func, ulong arg1, ulong arg2)
{
   struct tasklet_thread *t = tasklet_threads[tn];
   struct task *curr = get_curr_task();
   bool success, was_empty;

   (void) curr; // unused in some paths (unit tests case)
   ASSERT(t != NULL);

   struct tasklet new_tasklet = {
      .fptr = func,
      .ctx = {
         .arg1 = arg1,
         .arg2 = arg2,
      }
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
      tasklet_wakeup_runner(t);
   }

   enable_preemption();
   return success;
}

bool run_one_tasklet(int tn)
{
   bool success;
   struct tasklet tasklet_to_run;
   struct tasklet_thread *t = tasklet_threads[tn];

   ASSERT(t != NULL);
   success = safe_ringbuf_read_elem(&t->rb, &tasklet_to_run);

   if (success) {
      /* Run the tasklet with preemption enabled */
      tasklet_to_run.fptr(tasklet_to_run.ctx.arg1, tasklet_to_run.ctx.arg2);
   }

   return success;
}

/*
 * Debug-only checks useful to verify that kernel_yield() + context_switch()
 * do NOT change the current ESP. Sure, at some point when we decide that
 * those function will never be touched again we could remove this code, but
 * until then, in a fast-growing and changing code base like the current one,
 * it makes sense to constantly check that there are not subtle bugs in the
 * probably most critical code. The tasklet runner kernel thread seems the
 * perfect place for such checks, because it really often yields and gets the
 * control back. The DEBUG_VALIDATE_STACK_PTR() sure works as well, but it
 * catches bugs only when the stack pointer is completely out of the allocated
 * stack area for the current task. This following code allows instead, any kind
 * of such problems to be caught much earlier.
 */
#if !defined(NDEBUG) && !defined(RELEASE)

#define DEBUG_SAVE_ESP()                  \
   ulong curr_esp;                        \
   ulong saved_esp = get_stack_ptr();

#define DEBUG_CHECK_ESP()                                                 \
      curr_esp = get_stack_ptr();                                         \
      if (curr_esp != saved_esp)                                          \
         panic("ESP changed. Saved: %p, Curr: %p", saved_esp, curr_esp);

#else

#define DEBUG_SAVE_ESP()
#define DEBUG_CHECK_ESP()

#endif

void tasklet_runner(void *arg)
{
   struct tasklet_thread *t = arg;
   bool tasklet_run;
   ulong var;

   ASSERT(t != NULL);
   DEBUG_SAVE_ESP()

   while (true) {

      DEBUG_CHECK_ESP()
      t->waiting_for_jobs = false;

      do {

         tasklet_run = run_one_tasklet(t->thread_index);

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

struct task *get_hi_prio_ready_tasklet_runner(void)
{
   ASSERT(!is_preemption_enabled());
   struct tasklet_thread *selected = NULL;

   for (u32 i = 0; i < tasklet_threads_count; i++) {

      struct tasklet_thread *t = tasklet_threads[i];

      if (!any_tasklets_to_run(i))
         continue;

      if (!selected || t->priority < selected->priority)
         if (t->task->state == TASK_STATE_RUNNABLE)
            selected = t;
   }

   return selected ? selected->task : NULL;
}

int create_tasklet_thread(int priority, u16 limit)
{
   struct tasklet_thread *t;
   int rc;

   ASSERT(!is_preemption_enabled());
   DEBUG_ONLY(check_not_in_irq_handler());

   if (tasklet_threads_count >= ARRAY_SIZE(tasklet_threads))
      return -ENFILE; /* too many tasklet runners */

   t = kzmalloc(sizeof(struct tasklet_thread));

   if (!t)
      return -ENOMEM;

   t->thread_index = (int)tasklet_threads_count;
   t->priority = priority;
   t->limit = limit;
   t->tasklets = kzmalloc(sizeof(struct tasklet) * limit);

   if (!t->tasklets) {
      kfree2(t, sizeof(struct tasklet_thread));
      return -ENOMEM;
   }

   safe_ringbuf_init(&t->rb,
                     limit,
                     sizeof(struct tasklet),
                     t->tasklets);

   if ((rc = tasklet_create_thread_for(t))) {
      kfree2(t->tasklets, sizeof(struct tasklet) * limit);
      kfree2(t, sizeof(struct tasklet_thread));
      return rc;
   }

   tasklet_threads[t->thread_index] = t;

   /* Double-check that tasklet_threads_count did not change */
   ASSERT(t->thread_index == (int)tasklet_threads_count);
   tasklet_threads_count++;
   return t->thread_index;
}

void init_tasklets(void)
{
   int tn;

   tasklet_threads_count = 0;
   tn = create_tasklet_thread(0 /* priority */, MAX_PRIO_TASKLET_QUEUE_SIZE);

   if (tn < 0)
      panic("init_tasklet_thread() failed");

   ASSERT(tn == 0);
}
