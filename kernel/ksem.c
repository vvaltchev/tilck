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
   u64 start_ticks = 0, end_ticks = 0;
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

      prepare_to_wait_on(WOBJ_SEM, s, (u32)units, &s->wait_list);

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

int ksem_signal(struct ksem *s, int units)
{
   struct wait_obj *wo, *tmp;
   int rem_counter, rc = 0;
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

   s->counter += units;
   rem_counter = s->counter;

   list_for_each(wo, tmp, &s->wait_list, wait_list_node) {

      if (rem_counter <= 0)
         break; /* not enough units to unblock anybody */

      int wait_units = (int)wo->extra;
      ASSERT(wo->type == WOBJ_SEM);
      ASSERT(wait_units > 0);

      if (wait_units <= rem_counter) {
         struct task *ti = CONTAINER_OF(wo, struct task, wobj);
         rem_counter -= wait_units;
         wake_up(ti);
      }
   }

out:
   enable_preemption();
   return rc;
}
