
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/process.h>
#include <exos/sync.h>

static kcond cond = { 0 };
static kmutex cond_mutex = { 0 };

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
   kmutex_lock(&cond_mutex);

   printk("[thread signal]: under lock, waiting some time..\n");
   kernel_sleep(TIMER_HZ / 2);

   printk("[thread signal]: under lock, signal_all!\n");

   kcond_signal_all(&cond);
   kmutex_unlock(&cond_mutex);

   printk("[thread signal]: exit\n");

   printk("Run thread kcond_thread_wait_ticks\n");

   disable_preemption();
   {
      kthread_create(&kcond_thread_wait_ticks, NULL);
   }
   enable_preemption();
}

// TODO: make this selftest to terminate at the right moment (implement join)
void selftest_kcond(void)
{
   kmutex_init(&cond_mutex);
   kcond_init(&cond);

   kthread_create(&kcond_thread_test, (void*) 1);
   kthread_create(&kcond_thread_test, (void*) 2);
   kthread_create(&kcond_thread_signal_generator, NULL);
}
