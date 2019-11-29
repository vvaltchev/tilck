/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/irq.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/debug_utils.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/kb.h>

static volatile bool se_stop_requested;
static void (*se_running_func)(void);

bool se_is_stop_requested(void)
{
   return se_stop_requested;
}

void se_internal_run(void (*se_func)(void))
{
   /* Common self test setup code */
   disable_preemption();
   {
      se_stop_requested = false;
      se_running_func = se_func;
   }
   enable_preemption();

   /* Run the actual self test */
   se_func();

   /* Common self test tear down code */
   disable_preemption();
   {
      se_stop_requested = false;
      se_running_func = NULL;
   }
   enable_preemption();
}

static int se_keypress_handler(struct key_event ke)
{
   int ret = KB_HANDLER_NAK;

   if (!se_running_func)
      return ret;

   disable_preemption();

   if (se_running_func) {
      if (ke.print_char == 'c' && kb_is_ctrl_pressed()) {
         se_stop_requested = true;
         ret = KB_HANDLER_OK_AND_STOP;
      }
   }

   enable_preemption();
   return ret;
}

static struct keypress_handler_elem se_handler =
{
   .handler = &se_keypress_handler
};

__attribute__((constructor))
static void init_self_tests_infrastructure()
{
   kb_register_keypress_handler(&se_handler);
}

void regular_self_test_end(void)
{
   printk("Self-test completed.\n");
}

void simple_test_kthread(void *arg)
{
   u32 i;
#if !defined(NDEBUG) && !defined(RELEASE)
   uptr esp;
   uptr saved_esp = get_stack_ptr();
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
      }
   }

   printk("[kthread] completed\n");
}

void selftest_kthread_med(void)
{
   int tid = kthread_create(simple_test_kthread, 0, (void *)1);

   if (tid < 0)
      panic("Unable to create the simple test kthread");

   kthread_join(tid);
   regular_self_test_end();
}

void selftest_sleep_short()
{
   const u64 wait_ticks = TIMER_HZ;
   u64 before = get_ticks();

   kernel_sleep(wait_ticks);

   u64 after = get_ticks();
   u64 elapsed = after - before;

   printk("[sleeping_kthread] elapsed ticks: %llu (expected: %llu)\n",
          elapsed, wait_ticks);

   VERIFY((elapsed - wait_ticks) <= TIMER_HZ/10);
   regular_self_test_end();
}

void selftest_join_med()
{
   int tid;

   printk("[selftest join] create the simple thread\n");

   if ((tid = kthread_create(simple_test_kthread, 0, (void *)0xAA0011FF)) < 0)
      panic("Unable to create simple_test_kthread");

   printk("[selftest join] join()\n");
   kthread_join(tid);

   printk("[selftest join] kernel thread exited\n");
   regular_self_test_end();
}

/*
 * Special selftest that cannot be run by the system test runner.
 * It has to be run manually by passing to the kernel -s panic_manual.
 */
void selftest_panic_manual(void)
{
   printk("[panic selftest] I'll panic now\n");
   panic("test panic [str: '%s'][num: %d]", "test string", -123);
}

void selftest_panic2_manual(void)
{
   printk("[panic selftest] I'll panic with bad pointers\n");
   panic("test panic [str: '%s'][num: %d]", (char *)1234, -123);
}

/* This works as expected when the KERNEL_STACK_ISOLATION is enabled */
void selftest_so1_manual(void)
{
   char buf[16];

   /*
    * Hack needed to avoid the compiler detecting that we're accessing the
    * array out-of-bounds, which is generally a terrible bug. But here we're
    * looking exactly for this.
    */
   char *volatile ptr = buf;
   printk("Causing intentionally a stack overflow: expect panic\n");
   memset(ptr, 'x', KERNEL_STACK_SIZE);
}

/* This works as expected when the KERNEL_STACK_ISOLATION is enabled */
void selftest_so2_manual(void)
{
   char buf[16];

   /*
    * Hack needed to avoid the compiler detecting that we're accessing the
    * array below bounds, which is generally a terrible bug. But here we're
    * looking exactly for this.
    */
   char *volatile ptr = buf;
   printk("Causing intentionally a stack underflow: expect panic\n");
   memset(ptr - KERNEL_STACK_SIZE, 'y', KERNEL_STACK_SIZE);
}

static void NO_INLINE do_cause_double_fault(void)
{
   char buf[KERNEL_STACK_SIZE];
   memset(buf, 'z', KERNEL_STACK_SIZE);
}

void selftest_so3_manual(void)
{
   printk("Causing intentionally a double fault: expect panic\n");
   do_cause_double_fault();
}
