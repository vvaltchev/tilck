
#include <common_defs.h>
#include <string_util.h>
#include <term.h>
#include <irq.h>
#include <kmalloc.h>
#include <paging.h>
#include <debug_utils.h>
#include <process.h>

#include <hal.h>
#include <utils.h>
#include <tasklet.h>
#include <sync.h>

#define INIT_PROGRAM_MEM_DISK_OFFSET 0x00023600


void simple_test_kthread(void *arg)
{
   printk("[kernel thread] This is a kernel thread, arg = %p\n", arg);

   for (int i = 0; i < 1024*(int)MB; i++) {
      if (!(i % (256*MB))) {
         printk("[kernel thread] i = %i\n", i/MB);
      }
   }
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

void test_memdisk()
{
   char *ptr;

   printk("Data at %p:\n", 0x0);
   ptr = (char *)RAM_DISK_VADDR;
   for (int i = 0; i < 16; i++) {
      printk("%x ", (u8)ptr[i]);
   }
   printk("\n");

   printk("Data at %p:\n", INIT_PROGRAM_MEM_DISK_OFFSET);
   ptr = (char *)(RAM_DISK_VADDR + INIT_PROGRAM_MEM_DISK_OFFSET);
   for (int i = 0; i < 16; i++) {
      printk("%x ", (u8)ptr[i]);
   }
   printk("\n");



   printk("\n\n");
   printk("Calculating CRC32...\n");
   u32 crc = crc32(0, (void *)RAM_DISK_VADDR, RAM_DISK_SIZE);
   printk("Crc32 of the data: %p\n", crc);
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


void kmutex_test()
{
   kmutex_init(&test_mutex);
   current = kthread_create(test_kmutex_thread, (void *)1);
   current = kthread_create(test_kmutex_thread, (void *)2);
   current = kthread_create(test_kmutex_thread_trylock, NULL);
}


static kcond cond = { 0 };
static kmutex cond_mutex = { 0 };

void kcond_thread_test(void *arg)
{
   kmutex_lock(&cond_mutex);

   printk("[thread %i]: under lock, waiting for signal..\n", arg);
   kcond_wait(&cond, &cond_mutex);

   printk("[thread %i]: under lock, signal received..\n", arg);

   kmutex_unlock(&cond_mutex);

   printk("[thread %i]: exit\n", arg);
}


void kcond_thread_signal_generator()
{
   kmutex_lock(&cond_mutex);

   printk("[thread signal]: under lock, waiting some time..\n");
   for (int i=0; i < 1024*1024*1024; i++) { }

   printk("[thread signal]: under lock, signal_all!\n");

   kcond_signal_all(&cond);

   kmutex_unlock(&cond_mutex);

   printk("[thread signal]: exit\n");
}


void kcond_test()
{
   kmutex_init(&cond_mutex);
   kcond_init(&cond);

   current = kthread_create(&kcond_thread_test, (void*) 1);
   current = kthread_create(&kcond_thread_test, (void*) 2);
   current = kthread_create(&kcond_thread_signal_generator, NULL);
}
