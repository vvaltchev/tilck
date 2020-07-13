/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file contains functions used by tasklet.c, but are NOT placed there
 * in order to allow unit tests to wrap them (with -Wl,--wrap).
 */

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/errno.h>

#include "tasklet_int.h"

int tasklet_create_thread_for(struct tasklet_thread *t)
{
   const int tid = kthread_create(tasklet_runner, 0, t);

   if (tid < 0)
      return -ENOMEM;

   t->task = get_task(tid);
   t->task->tasklet_thread = t;
   return 0;
}

void tasklet_wakeup_runner(struct tasklet_thread *t)
{
   struct task *curr = get_curr_task();
   enum task_state exp_state = TASK_STATE_SLEEPING;

   t->waiting_for_jobs = false;
   atomic_cas_strong(&t->task->state,
                     &exp_state,
                     TASK_STATE_RUNNABLE,
                     mo_relaxed, mo_relaxed);
   /*
    * Note: we don't care whether atomic_cas_strong() succeeded or not.
    * Reason: if it didn't succeed, that's because an IRQ preempted us
    * and made its state to be runnable.
    */

   struct tasklet_thread *curr_tt = curr->tasklet_thread;

   if (!curr_tt || t->priority < curr_tt->priority)
      sched_set_need_resched();
}
