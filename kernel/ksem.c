/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/sync.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/errno.h>

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

int ksem_wait(struct ksem *s, int units)
{
   ASSERT(units > 0);

   if (s->max != KSEM_NO_MAX) {
      if (units > s->max)
         return -EPERM;
   }

   disable_preemption();

   while (s->counter < units) {

      task_set_wait_obj(get_curr_task(), WOBJ_SEM, s, NO_EXTRA, &s->wait_list);

      enable_preemption_nosched();
      kernel_yield();        /* won't wakeup by a signal here, see signal.c */
      disable_preemption();
   }

   s->counter -= units;
   enable_preemption();
   return 0;
}

static void
ksem_wakeup_task(struct ksem *s)
{
   struct wait_obj *task_wo =
      list_first_obj(&s->wait_list, struct wait_obj, wait_list_node);

   struct task *ti = CONTAINER_OF(task_wo, struct task, wobj);

   ASSERT(ti->state == TASK_STATE_SLEEPING);
   task_reset_wait_obj(ti);
}

int ksem_signal(struct ksem *s, int units)
{
   int rc = 0;
   int max_to_wakeup;
   ASSERT(units > 0);

   disable_preemption();

   if (s->max != KSEM_NO_MAX) {

      if (units > s->max || s->counter > s->max - units) {

         /*
          * NOTE: `s->counter + units > s->max` got re-written to avoid integer
          * wrap-around.
          */
         rc = -EPERM;
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
