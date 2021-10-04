/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/sync.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/timer.h>

void ksem_init(struct ksem *s, int val, int max)
{
   ASSERT(max == KSEM_NO_MAX || max > 0);
   s->max = max;
   s->counter = val;
   list_init(&s->wait_list);
}

void ksem_destroy(struct ksem *s)
{
   ASSERT(list_is_empty(&s->wait_list));
   bzero(s, sizeof(struct ksem));
}

static void
ksem_do_wait(struct ksem *s, int units, int timeout_ticks)
{
   u64 start_ticks, end_ticks;
   struct task *curr = get_curr_task();
   ASSERT(!is_preemption_enabled());

   if (timeout_ticks > 0) {

      start_ticks = get_ticks();
      end_ticks = start_ticks + (u32)timeout_ticks;

      if (s->counter < units)
         task_set_wakeup_timer(curr, (u32)timeout_ticks);

   } else {

      ASSERT(timeout_ticks == KSEM_WAIT_FOREVER);
   }

   while (s->counter < units) {

      if (timeout_ticks > 0) {
         if (get_ticks() >= end_ticks)
            break;
      }

      prepare_to_wait_on(WOBJ_SEM, s, NO_EXTRA, &s->wait_list);

      /* won't wakeup by a signal here, see signal.c */
      enter_sleep_wait_state();

      /* here the preemption is guaranteed to be enabled */
      disable_preemption();
   }

   if (timeout_ticks > 0)
      task_cancel_wakeup_timer(curr);
}

int ksem_wait(struct ksem *s, int units, int timeout_ticks)
{
   int rc = -ETIME;
   ASSERT(units > 0);

   if (s->max != KSEM_NO_MAX) {
      if (units > s->max)
         return -EINVAL;
   }

   disable_preemption();
   {
      if (timeout_ticks != KSEM_NO_WAIT)
         ksem_do_wait(s, units, timeout_ticks);

      if (s->counter >= units) {
         s->counter -= units;
         rc = 0;
      }
   }
   enable_preemption();
   return rc;
}

static void
ksem_wakeup_task(struct ksem *s)
{
   struct wait_obj *task_wo =
      list_first_obj(&s->wait_list, struct wait_obj, wait_list_node);

   struct task *ti = CONTAINER_OF(task_wo, struct task, wobj);

   ASSERT_TASK_STATE(ti->state, TASK_STATE_SLEEPING);
   wake_up(ti);
}

int ksem_signal(struct ksem *s, int units)
{
   int rc = 0;
   int max_to_wakeup;
   ASSERT(units > 0);

   disable_preemption();

   if (s->max != KSEM_NO_MAX) {

      if (units > s->max) {
         rc = -EINVAL;
         goto out;
      }

      if (s->counter > s->max - units) {

         /*
          * NOTE: `s->counter + units > s->max` got re-written to avoid integer
          * wrap-around.
          */
         rc = -EDQUOT;
         goto out;
      }
   }

   /* OK, it's safe to increase the counter by `units` */
   s->counter += units;

   if (s->counter <= 0)
      goto out; /* not enough units to unblock anybody */

   /*
    * Now, we don't know each of the waiters for how many units is waiting for.
    * The only thing we know is that each waiter must be waiting for AT LEAST
    * 1 unit. How many waiters should we wake up at most? If the available
    * units (counter) are less than `units`, wake `counter` waiters. Otherwise,
    * we have to be careful (in order to avoid waste of resourced): we should
    * NOT wake up more than `units` waiters, even if `counter` is much bigger.
    * Why? They were waiting with `counter > 0` even before this SIGNAL and
    * didn't stop waiting because the counter's value was not big enough.
    * Therefore, we've contributed to its value with `units` so, wake up at most
    * `units` waiters.
    */

   max_to_wakeup = MIN(s->counter, units);

   for (int i = 0; i < max_to_wakeup; i++) {

      if (list_is_empty(&s->wait_list))
         break;

      ksem_wakeup_task(s);
   }

out:
   enable_preemption();
   return rc;
}
