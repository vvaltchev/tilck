
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>
#include <exos/common/utils.h>

#include <exos/kernel/irq.h>
#include <exos/kernel/kmalloc.h>
#include <exos/kernel/paging.h>
#include <exos/kernel/debug_utils.h>
#include <exos/kernel/process.h>

#include <exos/kernel/hal.h>
#include <exos/kernel/tasklet.h>
#include <exos/kernel/sync.h>
#include <exos/kernel/fault_resumable.h>
#include <exos/kernel/timer.h>

void simple_test_kthread(void *arg)
{
   u32 i;
   DEBUG_ONLY(uptr esp);
   DEBUG_ONLY(uptr saved_esp = get_curr_stack_ptr());

   printk("[kthread] This is a kernel thread, arg = %p\n", arg);

   for (i = 0; i < 256*MB; i++) {

#ifdef DEBUG

      /*
       * This VERY IMPORTANT check ensures us that in NO WAY functions like
       * save_current_task_state() and kernel_context_switch() changed value
       * of the stack pointer. Unfortunately, we cannot reliably do this check
       * in RELEASE (= optimized) builds because the compiler plays with the
       * stack pointer and 'esp' and 'saved_esp' differ by a constant value.
       */
      esp = get_curr_stack_ptr();

      if (esp != saved_esp)
         panic("esp: %p != saved_esp: %p [curr-saved: %d], i = %u",
               esp, saved_esp, esp - saved_esp, i);

#endif

      if (!(i % (64*MB))) {
         printk("[kthread] i = %i\n", i/MB);
      }
   }

   printk("[kthread] completed\n");

   if ((uptr)arg == 1) {
      printk("[kthread] DEBUG QEMU turn off machine\n");
      debug_qemu_turn_off_machine();
   }
}

void selftest_kthread(void)
{
   kthread_create(simple_test_kthread, (void *)1);
}

void selftest_kernel_sleep()
{
   const u64 wait_ticks = TIMER_HZ;
   u64 before = get_ticks();

   kernel_sleep(wait_ticks);

   u64 after = get_ticks();
   u64 elapsed = after - before;

   printk("[sleeping_kthread] elapsed ticks: %llu (expected: %llu)\n",
          elapsed, wait_ticks);

   VERIFY((elapsed - wait_ticks) <= 2);

   debug_qemu_turn_off_machine();
}

void selftest_panic(void)
{
   printk("[panic selftest] In a while, I'll panic\n");

   for (int i = 0; i < 500*1000*1000; i++) {
      asmVolatile("nop");
   }

   panic("test panic");
}

void selftest_join()
{
   printk("[selftest join] create the simple thread\n");

   task_info *ti = kthread_create(simple_test_kthread, (void *)0xAA0011FF);

   printk("[selftest join] join()\n");

   join_kernel_thread(ti->tid);

   printk("[selftest join] kernel thread exited\n");
   debug_qemu_turn_off_machine();
}

