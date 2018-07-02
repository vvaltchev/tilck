
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/tasklet.h>
#include <exos/kmalloc.h>
#include <exos/hal.h>
#include <exos/sync.h>
#include <exos/process.h>
#include <exos/timer.h>

#include "tasklet_int.h"

static void tasklet_runner_kthread(void *arg);
tasklet_thread_info *tasklet_threads[MAX_TASKLET_THREADS];

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

   return !ringbuf_is_empty(&t->tasklet_ringbuf);
}

int create_tasklet_thread(int tn, int limit)
{
   tasklet_thread_info *t;

   /* CONTRACT: the tasklet thread must not aleady exist */
   ASSERT(tasklet_threads[tn] == NULL);

   t = kzmalloc(sizeof(tasklet_thread_info));

   if (!t)
      return -1;

   t->limit = limit;
   t->all_tasklets = kzmalloc(sizeof(tasklet) * limit);

   if (!t->all_tasklets) {
      kfree2(t, sizeof(tasklet_thread_info));
      return -1;
   }

   kcond_init(&t->tasklet_cond);
   ringbuf_init(&t->tasklet_ringbuf, limit, sizeof(tasklet), t->all_tasklets);

#ifndef UNIT_TEST_ENVIRONMENT

   t->task = kthread_create(tasklet_runner_kthread, (void *)(uptr)tn);

   if (!t->task) {
      kfree2(t->all_tasklets, sizeof(tasklet) * limit);
      kfree2(t, sizeof(tasklet_thread_info));
      return -1;
   }

#endif

   tasklet_threads[tn] = t;
   return 0;
}

/*
 * WARNING: this function is completely UNSAFE. The caller has to completely
 * take care of ensuring that:
 *
 *    - preemption is disabled
 *    - the tasklet thread 'tn' has no tasklets to run
 *    - no interrupt handler will try to enqueue a task on the thread 'tn'
 *      while destroy_tasklet_thread() is running and after it.
 *
 * NOTE: currently, this function is used only by unit-tests.
 */
void destroy_tasklet_thread(int tn)
{
   ASSERT(!is_preemption_enabled());

   tasklet_thread_info *t = tasklet_threads[tn];
   ASSERT(t != NULL);

   ASSERT(ringbuf_is_empty(&t->tasklet_ringbuf));

   kcond_destory(&t->tasklet_cond);
   ringbuf_destory(&t->tasklet_ringbuf);
   kfree2(t->all_tasklets, sizeof(tasklet) * t->limit);
   kfree2(t, sizeof(tasklet_thread_info));
   tasklet_threads[tn] = NULL;
}

void init_tasklets(void)
{
   if (create_tasklet_thread(0, 128))
      panic("init_tasklet_thread() failed");
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

   success = ringbuf_write_elem(&t->tasklet_ringbuf, &new_tasklet);


#ifndef UNIT_TEST_ENVIRONMENT

   /*
    * Special way of signalling a condition variable, without holding its lock:
    * this code will be often often called by higher-halfs of interrupt handlers
    * so it won't be possible to acquire a lock there. This means that the wait
    * on the other side may miss a signal (not waiting while we fire the signal
    * here) but that's OK since the tasklet runner thread calls run_one_tasklet
    * in a while(true) loop and it uses a timeout.
    */
   kcond_signal_single(&t->tasklet_cond, t->task);

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
      success = ringbuf_read_elem(&t->tasklet_ringbuf, &tasklet_to_run);
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
#ifdef DEBUG

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

      kcond_wait(&t->tasklet_cond, NULL, TIMER_HZ / 10);
   }
}
