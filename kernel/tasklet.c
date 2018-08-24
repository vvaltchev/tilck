
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/errno.h>

#include "tasklet_int.h"

u32 tasklet_threads_count;
tasklet_thread_info *tasklet_threads[MAX_TASKLET_THREADS];

int get_tasklet_runner_limit(int tn)
{
   ASSERT(tn >= 0 && tn < MAX_TASKLET_THREADS);
   tasklet_thread_info *t = tasklet_threads[tn];
   return t ? t->limit : 0;
}

task_info *get_tasklet_runner(int tn)
{
   tasklet_thread_info *t = tasklet_threads[tn];

   if (!t)
      return NULL;

   ASSERT(t->task != NULL);
   return t->task;
}

bool any_tasklets_to_run(int tn)
{
   tasklet_thread_info *t = tasklet_threads[tn];

   if (!t)
      return false;

   return !ringbuf_is_empty(&t->ringbuf);
}

bool enqueue_tasklet_int(int tn, void *func, uptr arg1, uptr arg2)
{
   tasklet_thread_info *t = tasklet_threads[tn];
   ASSERT(t != NULL);

   bool success;

   tasklet new_tasklet = {
      .fptr = func,
      .ctx = {
         .arg1 = arg1,
         .arg2 = arg2
      }
   };

   disable_preemption();


#ifndef UNIT_TEST_ENVIRONMENT

   /*
    * Trying to enqueue a tasklet from the same tasklet thread can cause
    * a deadlock when the ringbuf is full if the caller waits in a loop for
    * the enqueue to succeed: the runner function won't get the control back
    * until it gets the control to execute a tasklet and, clearly, this is a
    * contraddition, leading to an endless loop. Exception: if we're running
    * in IRQ that interrupted the current task, which might be the tasklet
    * runner we'd like to enqueue in, we have to allow the enqueue to happen.
    * Simple example: a key press generates an IRQ #1 which interrupts the
    * tasklet runner #1 and wants to enqueue a tasklet there. We MUST allow
    * that to happen.
    */

   #ifdef DEBUG

      if (get_curr_task() == t->task)
         check_in_irq_handler();

   #endif

#endif

   success = ringbuf_write_elem(&t->ringbuf, &new_tasklet);


#ifndef UNIT_TEST_ENVIRONMENT

   /*
    * Special way of signalling a condition variable, without holding its lock:
    * this code will be often often called by higher-halfs of interrupt handlers
    * so it won't be possible to acquire a lock there. This means that the wait
    * on the other side may miss a signal (not waiting while we fire the signal
    * here) but that's OK since the tasklet runner thread calls run_one_tasklet
    * in a while(true) loop and it uses a timeout.
    */
   kcond_signal_single(&t->cond, t->task);

#endif


   enable_preemption();
   return success;
}

bool run_one_tasklet(int tn)
{
   tasklet_thread_info *t = tasklet_threads[tn];
   ASSERT(t != NULL);

   bool success;
   tasklet tasklet_to_run;

   disable_preemption();
   {
      success = ringbuf_read_elem(&t->ringbuf, &tasklet_to_run);
   }
   enable_preemption();

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

#define DEBUG_SAVE_ESP()                 \
   uptr curr_esp;                        \
   uptr saved_esp = get_stack_ptr();

#define DEBUG_CHECK_ESP()                                                 \
      curr_esp = get_stack_ptr();                                         \
      if (curr_esp != saved_esp)                                          \
         panic("ESP changed. Saved: %p, Curr: %p", saved_esp, curr_esp);

#else

#define DEBUG_SAVE_ESP()
#define DEBUG_CHECK_ESP()

#endif

static void tasklet_runner_kthread(void *arg)
{
   bool tasklet_run;
   int tn = (int)(uptr)arg;
   tasklet_thread_info *t = tasklet_threads[tn];
   ASSERT(t != NULL);

   DEBUG_SAVE_ESP()

   while (true) {

      DEBUG_CHECK_ESP()

      do {

         tasklet_run = run_one_tasklet(tn);

      } while (tasklet_run);

      kcond_wait(&t->cond, NULL, TIMER_HZ / 10);
   }
}

task_info *get_highest_runnable_priority_tasklet_runner(void)
{
   ASSERT(!is_preemption_enabled());

   tasklet_thread_info *selected = NULL;

   for (u32 i = 0; i < tasklet_threads_count; i++) {

      tasklet_thread_info *t = tasklet_threads[i];

      if (!any_tasklets_to_run(i))
         continue;

      if (!selected || t->priority < selected->priority)
         if (t->task->state == TASK_STATE_RUNNABLE)
            selected = t;
   }

   return selected ? selected->task : NULL;
}


int create_tasklet_thread(int priority, int limit)
{
   ASSERT(!is_preemption_enabled());
   DEBUG_ONLY(check_not_in_irq_handler());

   tasklet_thread_info *t;

   if (tasklet_threads_count >= ARRAY_SIZE(tasklet_threads))
      return -ENFILE; /* too many tasklet runner threads */

   t = kzmalloc(sizeof(tasklet_thread_info));

   if (!t)
      return -ENOMEM;

   int tn = tasklet_threads_count;
   t->priority = priority;
   t->limit = limit;
   t->tasklets = kzmalloc(sizeof(tasklet) * limit);

   if (!t->tasklets) {
      kfree2(t, sizeof(tasklet_thread_info));
      return -ENOMEM;
   }

   kcond_init(&t->cond);
   ringbuf_init(&t->ringbuf, limit, sizeof(tasklet), t->tasklets);

#ifndef UNIT_TEST_ENVIRONMENT

   t->task = kthread_create(tasklet_runner_kthread, (void *)(uptr)tn);

   if (!t->task) {
      kfree2(t->tasklets, sizeof(tasklet) * limit);
      kfree2(t, sizeof(tasklet_thread_info));
      return -ENOMEM;
   }

#endif

   tasklet_threads[tn] = t;

   /* Double-check that tasklet_threads_count did not change */
   ASSERT(tn == (int)tasklet_threads_count);

   tasklet_threads_count++;
   return tn;
}

void destroy_last_tasklet_thread(void)
{
   ASSERT(!is_preemption_enabled());
   ASSERT(tasklet_threads_count > 0);
   DEBUG_ONLY(check_not_in_irq_handler());

   int tn = --tasklet_threads_count;
   tasklet_thread_info *t = tasklet_threads[tn];
   ASSERT(t != NULL);

#ifndef UNIT_TEST_ENVIRONMENT
   ASSERT(ringbuf_is_empty(&t->ringbuf));
#endif

   kcond_destory(&t->cond);
   ringbuf_destory(&t->ringbuf);
   kfree2(t->tasklets, sizeof(tasklet) * t->limit);
   kfree2(t, sizeof(tasklet_thread_info));
   bzero(t, sizeof(*t));
   tasklet_threads[tn] = NULL;
}

void init_tasklets(void)
{
   int tn;

   tasklet_threads_count = 0;
   tn = create_tasklet_thread(0 /* priority */, 32 /* max tasklets */);

   if (tn < 0)
      panic("init_tasklet_thread() failed");

   ASSERT(tn == 0);
}
