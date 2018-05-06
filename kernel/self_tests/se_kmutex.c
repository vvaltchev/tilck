
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/utils.h>

#include <exos/process.h>
#include <exos/sync.h>

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

void selftest_kmutex()
{
   kmutex_init(&test_mutex);
   kthread_create(&simple_test_kthread, (void*)0xAA1234BB);
   kthread_create(test_kmutex_thread, (void *)1);
   kthread_create(test_kmutex_thread, (void *)2);
   kthread_create(test_kmutex_thread_trylock, NULL);
}
