/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/timer.h>

void simple_test_kthread(void *arg)
{
   u32 i;
#if !defined(NDEBUG) && !defined(RELEASE)
   ulong esp;
   ulong saved_esp = get_stack_ptr();
#endif

   printk("[kthread] This is a kernel thread, arg = %p\n", arg);

   for (i = 0; i < 128*MB; i++) {

#if !defined(NDEBUG) && !defined(RELEASE)

      /*
       * This VERY IMPORTANT check ensures us that in NO WAY functions like
       * save_current_task_state() and kernel_context_switch() changed value
       * of the stack pointer. Unfortunately, we cannot reliably do this check
       * in RELEASE (= optimized) builds because the compiler plays with the
       * stack pointer and 'esp' and 'saved_esp' differ by a constant value.
       */
      esp = get_stack_ptr();

      if (esp != saved_esp)
         panic("esp: %p != saved_esp: %p [curr-saved: %d], i = %u",
               esp, saved_esp, esp - saved_esp, i);

#endif

      if (!(i % (8*MB))) {

         printk("[kthread] i = %i\n", i/MB);

         if (se_is_stop_requested())
            break;
      }
   }

   printk("[kthread] completed\n");
}

void selftest_kthread(void)
{
   int tid = kthread_create(simple_test_kthread, 0, (void *)1);

   if (tid < 0)
      panic("Unable to create the simple test kthread");

   kthread_join(tid, true);

   if (se_is_stop_requested())
      se_interrupted_end();
   else
      se_regular_end();
}

REGISTER_SELF_TEST(kthread, se_med, &selftest_kthread)

void selftest_sleep()
{
   const u64 wait_ticks = TIMER_HZ;
   u64 before = get_ticks();

   kernel_sleep(wait_ticks);

   u64 after = get_ticks();
   u64 elapsed = after - before;

   printk("[sleeping_kthread] elapsed ticks: %" PRIu64
          " (expected: %" PRIu64 ")\n", elapsed, wait_ticks);

   VERIFY((elapsed - wait_ticks) <= TIMER_HZ/10);
   se_regular_end();
}

REGISTER_SELF_TEST(sleep, se_short, &selftest_sleep)

void selftest_join()
{
   int tid;

   printk("[selftest join] create the simple thread\n");

   if ((tid = kthread_create(simple_test_kthread, 0, (void *)0xAA0011FF)) < 0)
      panic("Unable to create simple_test_kthread");

   printk("[selftest join] join()\n");
   kthread_join(tid, true);

   printk("[selftest join] kernel thread exited\n");

   if (se_is_stop_requested())
      se_interrupted_end();
   else
      se_regular_end();
}

REGISTER_SELF_TEST(join, se_med, &selftest_join)
