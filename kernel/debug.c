/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/self_tests.h>

#include <elf.h>         // system header
#include <multiboot.h>   // system header in include/system_headers

/*
 * NOTE: this flag does affect sched_alive_thread() only when it is actually
 * running. By default it does *not* run. It gets activated only by the kernel
 * cmdline option -sat.
 */
static volatile bool sched_alive_thread_enabled = true;

static void sched_alive_thread()
{
   for (int counter = 0; ; counter++) {

      if (sched_alive_thread_enabled) {

         printk("---- Sched alive thread: %d ----\n", counter);

         if (KERNEL_SELFTESTS && (counter % 2)) {
            debug_check_for_deadlock();
            debug_check_for_any_progress();
         }
      }

      kernel_sleep(TIMER_HZ);
   }
}

void init_extra_debug_features(void)
{
   if (kopt_sched_alive_thread)
      if (kthread_create(&sched_alive_thread, 0, NULL) < 0)
         panic("Unable to create a kthread for sched_alive_thread()");
}

int set_sched_alive_thread_enabled(bool enabled)
{
   sched_alive_thread_enabled = enabled;
   return 0;
}

void kmain_early_checks(void)
{
   if (KERNEL_FORCE_TC_ISYSTEM)
      panic("Builds with KERNEL_FORCE_TC_ISYSTEM=1 are not supposed to run");
}

#if SLOW_DEBUG_REF_COUNT

/*
 * Set here the address of the ref_count to track.
 */
void *debug_refcount_obj = (void *)0;

/* Return the new value */
int __retain_obj(int *ref_count)
{
   int ret;
   ATOMIC(int) *atomic = (ATOMIC(int) *)ref_count;
   ret = atomic_fetch_add_explicit(atomic, 1, mo_relaxed) + 1;

   if (!debug_refcount_obj || ref_count == debug_refcount_obj) {
      printk(COLOR_GREEN "refcount at %p: %d -> %d" RESET_ATTRS "\n",
             ref_count, ret-1, ret);
   }

   return ret;
}

/* Return the new value */
int __release_obj(int *ref_count)
{
   int old, ret;
   ATOMIC(int) *atomic = (ATOMIC(int) *)ref_count;
   old = atomic_fetch_sub_explicit(atomic, 1, mo_relaxed);
   ASSERT(old > 0);
   ret = old - 1;

   if (!debug_refcount_obj || ref_count == debug_refcount_obj) {
      printk(COLOR_RED "refcount at %p: %d -> %d" RESET_ATTRS "\n",
             ref_count, old, ret);
   }

   return ret;
}
#endif
