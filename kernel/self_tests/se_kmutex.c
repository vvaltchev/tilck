/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/self_tests.h>

void simple_test_kthread(void *arg);

static kmutex test_mutex = { 0 };

static void test_kmutex_thread(void *arg)
{
   printk("%i) before lock\n", arg);

   kmutex_lock(&test_mutex);

   printk("%i) under lock..\n", arg);

   for (int i = 0; i < 128*MB; i++) {
      asmVolatile("nop");
   }

   kmutex_unlock(&test_mutex);

   printk("%i) after lock\n", arg);
}

static void test_kmutex_thread_trylock()
{
   printk("3) before trylock\n");

   bool locked = kmutex_trylock(&test_mutex);

   if (locked) {

      printk("3) trylock SUCCEEDED: under lock..\n");

      kmutex_unlock(&test_mutex);

      printk("3) after lock\n");

   } else {
      printk("3) trylock returned FALSE\n");
   }
}

void selftest_kmutex_med()
{
   int tid1, tid2, tid3, tid4;

   printk("kmutex basic test\n");
   kmutex_init(&test_mutex, 0);

   tid1 = kthread_create(&simple_test_kthread, NULL)->tid;
   tid2 = kthread_create(test_kmutex_thread, (void *)1)->tid;
   tid3 = kthread_create(test_kmutex_thread, (void *)2)->tid;
   tid4 = kthread_create(test_kmutex_thread_trylock, NULL)->tid;

   kthread_join(tid1);
   kthread_join(tid2);
   kthread_join(tid3);
   kthread_join(tid4);

   kmutex_destroy(&test_mutex);
   regular_self_test_end();
}

void selftest_kmutex2_med()
{
   bool success;
   int tid1, tid2, tid3;

   printk("kmutex recursive test\n");
   kmutex_init(&test_mutex, KMUTEX_FL_RECURSIVE);

   kmutex_lock(&test_mutex);
   printk("Locked once\n");

   kmutex_lock(&test_mutex);
   printk("Locked twice\n");

   success = kmutex_trylock(&test_mutex);

   if (!success) {
      panic("kmutex_trylock() failed on the same thread");
   }

   printk("Locked 3 times (last with trylock)\n");

   tid3 = kthread_create(test_kmutex_thread_trylock, NULL)->tid;
   kthread_join(tid3);

   kmutex_unlock(&test_mutex);
   printk("Unlocked once\n");

   kmutex_unlock(&test_mutex);
   printk("Unlocked twice\n");

   kmutex_unlock(&test_mutex);
   printk("Unlocked 3 times\n");

   tid1 = kthread_create(test_kmutex_thread, (void *)1)->tid;
   tid2 = kthread_create(test_kmutex_thread, (void *)2)->tid;
   tid3 = kthread_create(test_kmutex_thread_trylock, NULL)->tid;

   kthread_join(tid1);
   kthread_join(tid2);
   kthread_join(tid3);

   kmutex_destroy(&test_mutex);
   regular_self_test_end();
}
