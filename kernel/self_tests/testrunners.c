
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
#include <exos/fault_resumable.h>

void test_tasklet_func()
{
   for (int i = 0; i < 10; i++) {
      asmVolatile("nop");
   }
}

void tasklet_test_thread(void *unused)
{
   const int tot_iters = MAX_TASKLETS * 10;
   printk("[selftest_tasklet] BEGIN\n");

   for (int i = 0; i < tot_iters; i++) {

      ASSERT(is_preemption_enabled());

      bool added;

      do {
         disable_preemption();
         {
            added = enqueue_tasklet0(&test_tasklet_func);
         }
         enable_preemption();
      } while (!added);

      if (!(i % 1000)) {
         printk("[selftest_tasklet] %i%%\n", (i*100)/tot_iters);
      }
   }

   printk("[selftest_tasklet] COMPLETED\n");
   debug_qemu_turn_off_machine();
}

void selftest_tasklet(void)
{
   kthread_create(tasklet_test_thread, NULL);
}

void simple_test_kthread(void *arg)
{
   int i;
   uptr saved_esp;
   uptr esp;

   printk("[kthread] This is a kernel thread, arg = %p\n", arg);

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
         printk("[kthread] i = %i\n", i/MB);
      }
   }

   debug_qemu_turn_off_machine();
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

   printk("[sleeping_kthread] elapsed ticks: %llu (expected: %llu)\n",
          elapsed, wait_ticks);

   VERIFY((elapsed - wait_ticks) <= 2);

   debug_qemu_turn_off_machine();
}

void selftest_kernel_sleep(void)
{
   kthread_create(sleeping_kthread, (void *)TIMER_HZ);
}

static kmutex test_mutex = { 0 };

void test_kmutex_thread(void *arg)
{
   printk("%i) before lock\n", arg);

   kmutex_lock(&test_mutex);

   printk("%i) under lock..\n", arg);
   for (int i=0; i < 256*MB; i++) { }

   kmutex_unlock(&test_mutex);

   printk("%i) after lock\n", arg);
}

void test_kmutex_thread_trylock()
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

#ifdef __i386__

static void faulting_code_div0(void)
{
   asmVolatile("mov $0, %edx\n\t"
               "mov $1, %eax\n\t"
               "mov $0, %ecx\n\t"
               "div %ecx\n\t");
}

static void faulting_code(void)
{
   printk("hello from div by 0 faulting code\n");

   disable_preemption();

   faulting_code_div0();

   /*
    * Note: because the above asm will trigger a div by 0 fault, we'll never
    * reach the enable_preemption() below. This is an intentional way of testing
    * that fault_resumable_call() will restore correctly the value of
    * disable_preemption_count in case of fault.
    */

   enable_preemption();
}

static void faulting_code2(void)
{
   void **null_ptr = NULL;
   *null_ptr = NULL;
}

#define NESTED_FAULTING_CODE_MAX_LEVELS 4

static void nested_faulting_code(int level)
{
   if (level == NESTED_FAULTING_CODE_MAX_LEVELS) {
      printk("[level %i]: *** call faulting code ***\n", level);
      faulting_code2();
      NOT_REACHED();
   }

   printk("[level %i]: do recursive nested call\n", level);

   int r = fault_resumable_call((u32)-1, nested_faulting_code, 1, level+1);

   if (r) {
      if (level == NESTED_FAULTING_CODE_MAX_LEVELS - 1) {
         printk("[level %i]: the call faulted (r = %u). "
                "Let's do another faulty call\n", level, r);
         faulting_code_div0();
         NOT_REACHED();
      } else {
         printk("[level %i]: the call faulted (r = %u)\n", level, r);
      }
   } else {
      printk("[level %i]: the call was OK\n", level);
   }

   printk("[level %i]: we reached the end\n", level);
}

void selftest_fault_resumable(void)
{
   int r;

   printk("fault_resumable with just printk()\n");
   r = fault_resumable_call((u32)-1,
                            printk,
                            2,
                            "hi from fault resumable: %s\n",
                            "arg1");
   printk("returned %i\n", r);

   printk("fault_resumable with code causing div by 0\n");
   r = fault_resumable_call(1 << FAULT_DIVISION_BY_ZERO, faulting_code, 0);
   printk("returned %i\n", r);

   printk("fault_resumable with code causing page fault\n");
   r = fault_resumable_call(1 << FAULT_PAGE_FAULT, faulting_code2, 0);
   printk("returned %i\n", r);

   printk("[level 0]: do recursive nested call\n");
   r = fault_resumable_call((u32)-1, // all faults
                            nested_faulting_code,
                            1,  // nargs
                            1); // arg1: level
   printk("[level 0]: call returned %i\n", r);
   debug_qemu_turn_off_machine();
}

static NO_INLINE void do_nothing(uptr a1, uptr a2, uptr a3,
                                 uptr a4, uptr a5, uptr a6)
{
   DO_NOT_OPTIMIZE_AWAY(a1);
   DO_NOT_OPTIMIZE_AWAY(a2);
   DO_NOT_OPTIMIZE_AWAY(a3);
   DO_NOT_OPTIMIZE_AWAY(a4);
   DO_NOT_OPTIMIZE_AWAY(a5);
   DO_NOT_OPTIMIZE_AWAY(a6);
}

void selftest_fault_resumable_perf(void)
{
   const int iters = 100000;
   u64 start, duration;

   start = RDTSC();

   for (int i = 0; i < iters; i++)
      do_nothing(1,2,3,4,5,6);

   duration = RDTSC() - start;

   printk("regular call: %llu cycles\n", duration/iters);

   enable_preemption();
   {
      start = RDTSC();

      for (int i = 0; i < iters; i++)
         fault_resumable_call(0, do_nothing, 6, 1, 2, 3, 4, 5, 6);

      duration = RDTSC() - start;
   }
   disable_preemption();

   printk("fault resumable call: %llu cycles\n", duration/iters);
   debug_qemu_turn_off_machine();
}

#endif
