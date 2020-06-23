/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/self_tests.h>

static struct kcond cond = { 0 };
static struct kmutex cond_mutex = { 0 };

static void kcond_thread_test(void *arg)
{
   kmutex_lock(&cond_mutex);

   printk("[thread %i]: under lock, waiting for signal..\n", arg);
   bool success = kcond_wait(&cond, &cond_mutex, KCOND_WAIT_FOREVER);

   if (success)
      printk("[thread %i]: under lock, signal received..\n", arg);
   else
      panic("[thread %i]: under lock, kcond_wait() FAILED\n", arg);

   kmutex_unlock(&cond_mutex);

   printk("[thread %i]: exit\n", arg);
}

static void kcond_thread_wait_ticks()
{
   kmutex_lock(&cond_mutex);
   printk("[kcond wait ticks]: holding the lock, run wait()\n");

   bool success = kcond_wait(&cond, &cond_mutex, TIMER_HZ/2);

   if (!success)
      printk("[kcond wait ticks]: woke up due to timeout, as expected!\n");
   else
      panic("[kcond wait ticks] FAILED: kcond_wait() returned true.");

   kmutex_unlock(&cond_mutex);
}


static void kcond_thread_signal_generator()
{
   int tid;

   kmutex_lock(&cond_mutex);

   printk("[thread signal]: under lock, waiting some time..\n");
   kernel_sleep(TIMER_HZ / 2);

   printk("[thread signal]: under lock, signal_all!\n");

   kcond_signal_all(&cond);
   kmutex_unlock(&cond_mutex);

   printk("[thread signal]: exit\n");

   printk("Run thread kcond_thread_wait_ticks\n");

   if ((tid = kthread_create(&kcond_thread_wait_ticks, 0, NULL)) < 0)
      panic("Unable to create a thread for kcond_thread_wait_ticks()");

   kthread_join(tid);
}

void selftest_kcond_short()
{
   int tids[3];
   kmutex_init(&cond_mutex, 0);
   kcond_init(&cond);

   tids[0] = kthread_create(&kcond_thread_test, 0, (void*) 1);
   VERIFY(tids[0] > 0);

   tids[1] = kthread_create(&kcond_thread_test, 0, (void*) 2);
   VERIFY(tids[1] > 0);

   tids[2] = kthread_create(&kcond_thread_signal_generator, 0, NULL);
   VERIFY(tids[2] > 0);

   kthread_join_all(tids, ARRAY_SIZE(tids));
   kcond_destory(&cond);
   regular_self_test_end();
}
