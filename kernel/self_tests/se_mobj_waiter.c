/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/sync.h>

static kcond conds[2];

static void mobj_waiter_sig_thread(void *arg)
{
   uptr n = (uptr) arg;
   u64 ticks_to_sleep = (u64)(n + 1) * TIMER_HZ / 2;

   printk("[thread %u] sleep for %d ticks\n", n, ticks_to_sleep);
   kernel_sleep(ticks_to_sleep);

   printk("[thread %u] signal cond %d\n", n, n);
   kcond_signal_one(&conds[n]);
}

static void mobj_waiter_wait_thread(void *arg)
{
   multi_obj_waiter *w = allocate_mobj_waiter(ARRAY_SIZE(conds));
   VERIFY(w != NULL);

   for (u32 j = 0; j < ARRAY_SIZE(conds); j++)
      mobj_waiter_set(w, j, WOBJ_KCOND, &conds[j], &conds[j].wait_list);

   for (u32 i = 0; i < ARRAY_SIZE(conds); i++) {

      printk("[wait thread]: going to sleep on waiter obj\n");
      kernel_sleep_on_waiter(w);

      printk("[wait th ] wake up #%u\n", i);

      for (size_t j = 0; j < ARRAY_SIZE(conds); j++) {

         mwobj_elem *me = &w->elems[j];

         if (me->type && !me->wobj.type) {
            printk("[wait th ]    -> condition #%u was signaled\n", j);
            mobj_waiter_reset(me);
         }
      }
   }

   free_mobj_waiter(w);
}

void selftest_mobj_waiter_short()
{
   int tids[ARRAY_SIZE(conds)];
   int w_tid;

   for (size_t i = 0; i < ARRAY_SIZE(conds); i++) {
      kcond_init(&conds[i]);
      tids[i] = kthread_create(&mobj_waiter_sig_thread, (void*) i)->tid;
   }

   w_tid = kthread_create(&mobj_waiter_wait_thread, NULL)->tid;

   for (size_t i = 0; i < ARRAY_SIZE(conds); i++)
      kthread_join(tids[i]);

   kthread_join(w_tid);

   for (size_t i = 0; i < ARRAY_SIZE(conds); i++)
      kcond_destory(&conds[i]);

   regular_self_test_end();
}
