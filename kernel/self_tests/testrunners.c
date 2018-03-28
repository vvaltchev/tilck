
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/utils.h>

#include <exos/irq.h>
#include <exos/kmalloc.h>
#include <exos/paging.h>
#include <exos/debug_utils.h>
#include <exos/process.h>

#include <exos/hal.h>
#include <exos/tasklet.h>
#include <exos/sync.h>

void test_tasklet_func()
{
   for (int i = 0; i < 10; i++) {
      asmVolatile("nop");
   }
}

void selftest_tasklet()
{
   const int tot_iters = MAX_TASKLETS * 10;

   printk("[selftest_tasklet] BEGIN\n");

   for (int i = 0; i < tot_iters; i++) {

      bool added;

      do {
         added = enqueue_tasklet0(&test_tasklet_func);
      } while (!added);

      if (!(i % 1000)) {
         printk("[selftest_tasklet] %i%%\n", (i*100)/tot_iters);
      }
   }

   printk("[selftest_tasklet] COMPLETED\n");
}


void simple_test_kthread(void *arg)
{
   int i;
   uptr saved_esp;
   uptr esp;

   printk("[kernel thread] This is a kernel thread, arg = %p\n", arg);

   saved_esp = get_curr_stack_ptr();

   for (i = 0; i < 1024*(int)MB; i++) {

      /*
       * This VERY IMPORTANT check ensures us that in NO WAY functions like
       * save_current_task_state() and kernel_context_switch() changed value
       * of the stack pointer.
       */
      esp = get_curr_stack_ptr();
      VERIFY(esp == saved_esp);

      if (!(i % (256*MB))) {
         printk("[kernel thread] i = %i\n", i/MB);
      }
   }
}

void selftest_kthread(void)
{
   kthread_create(simple_test_kthread, (void *)0xAA0011FF);
}


void sleeping_kthread(void *arg)
{
   u64 wait_ticks = (uptr) arg;
   u64 before = get_ticks();

   kernel_sleep(wait_ticks);

   u64 after = get_ticks();
   u64 elapsed = after - before;

   printk("[Sleeping kthread] elapsed ticks: %llu (expected: %llu)\n",
          elapsed, wait_ticks);

   ASSERT((elapsed - wait_ticks) <= 2);
}


static kmutex test_mutex = { 0 };

void test_kmutex_thread(void *arg)
{
   printk("%i) before lock\n", arg);

   kmutex_lock(&test_mutex);

   printk("%i) under lock..\n", arg);
   for (int i=0; i < 1024*1024*1024; i++) { }

   kmutex_unlock(&test_mutex);

   printk("%i) after lock\n", arg);
}

void test_kmutex_thread_trylock()
{
   printk("3) before trylock\n");

   bool locked = kmutex_trylock(&test_mutex);

   if (locked) {

      printk("3) trylock SUCCEEDED: under lock..\n");

      if (locked) {
         kmutex_unlock(&test_mutex);
      }

      printk("3) after lock\n");

   } else {
      printk("trylock returned FALSE\n");
   }
}


void selftest_kmutex()
{
   kmutex_init(&test_mutex);
   kthread_create(&simple_test_kthread, (void*)0xAA1234BB);
   kthread_create(test_kmutex_thread, (void *)1);
   kthread_create(test_kmutex_thread, (void *)2);
   kthread_create(test_kmutex_thread_trylock, NULL);
}


static kcond cond = { 0 };
static kmutex cond_mutex = { 0 };

void kcond_thread_test(void *arg)
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

void kcond_thread_wait_ticks()
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


void kcond_thread_signal_generator()
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

void selftest_kcond()
{
   kmutex_init(&cond_mutex);
   kcond_init(&cond);

   kthread_create(&kcond_thread_test, (void*) 1);
   kthread_create(&kcond_thread_test, (void*) 2);
   kthread_create(&kcond_thread_signal_generator, NULL);
}

void kthread_panic(void)
{
   for (int i = 0; i < 1000*1000*1000; i++) {
      asmVolatile("nop");
   }

   panic("test panic");
}

void selftest_panic(void)
{
   kthread_create(&kthread_panic, NULL);
}
