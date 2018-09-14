/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/debug_utils.h>

void simple_test_kthread(void *arg);

static kmutex test_mutex = { 0 };

static void test_kmutex_thread(void *arg)
{
   printk("%i) before lock\n", arg);

   kmutex_lock(&test_mutex);

   printk("%i) under lock..\n", arg);
   for (int i=0; i < 256*MB; i++) { }

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

   kmutex_init(&test_mutex, 0);

   tid1 = kthread_create(&simple_test_kthread, NULL)->tid;
   tid2 = kthread_create(test_kmutex_thread, (void *)1)->tid;
   tid3 = kthread_create(test_kmutex_thread, (void *)2)->tid;
   tid4 = kthread_create(test_kmutex_thread_trylock, NULL)->tid;

   join_kernel_thread(tid1);
   join_kernel_thread(tid2);
   join_kernel_thread(tid3);
   join_kernel_thread(tid4);

   printk("Recursive mutex (basic) test\n");
   kmutex_destroy(&test_mutex);
   kmutex_init(&test_mutex, KMUTEX_FL_RECURSIVE);

   kmutex_lock(&test_mutex);
   printk("Locked once\n");

   kmutex_lock(&test_mutex);
   printk("Locked twice\n");

   bool success = kmutex_trylock(&test_mutex);
   VERIFY(success);
   printk("Locked 3 times (last with trylock)\n");

   tid4 = kthread_create(test_kmutex_thread_trylock, NULL)->tid;
   join_kernel_thread(tid4);

   kmutex_unlock(&test_mutex);
   printk("Unlocked once\n");

   kmutex_unlock(&test_mutex);
   printk("Unlocked twice\n");

   kmutex_unlock(&test_mutex);
   printk("Unlocked 3 times\n");

   tid2 = kthread_create(test_kmutex_thread, (void *)1)->tid;
   tid3 = kthread_create(test_kmutex_thread, (void *)2)->tid;
   tid4 = kthread_create(test_kmutex_thread_trylock, NULL)->tid;

   join_kernel_thread(tid2);
   join_kernel_thread(tid3);
   join_kernel_thread(tid4);

   kmutex_destroy(&test_mutex);
   debug_qemu_turn_off_machine();
}
