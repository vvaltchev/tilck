/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/sync.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/interrupts.h>

void kcond_init(struct kcond *c)
{
   DEBUG_ONLY(check_not_in_irq_handler());
   list_init(&c->wait_list);
}

bool kcond_is_anyone_waiting(struct kcond *c)
{
   bool ret;
   disable_preemption();
   {
      ret = !list_is_empty(&c->wait_list);
   }
   enable_preemption();
   return ret;
}

bool kcond_wait(struct kcond *c, struct kmutex *m, u32 timeout_ticks)
{
   DEBUG_ONLY(check_not_in_irq_handler());
   ASSERT(!m || kmutex_is_curr_task_holding_lock(m));
   struct task *curr = get_curr_task();
   bool ret;

   disable_preemption();

   prepare_to_wait_on(WOBJ_KCOND, c, NO_EXTRA, &c->wait_list);

   if (timeout_ticks != KCOND_WAIT_FOREVER)
      task_set_wakeup_timer(curr, timeout_ticks);

   if (m) {
      kmutex_unlock(m);
   }

   /* Go to sleep until a signal is fired or timeout happens */
   enter_sleep_wait_state();

   /* ------------------- We've been woken up ------------------- */

   /*
    * wait_obj_reset() returns the older value of wobj.ptr: in case it was
    * NULL, we'll return true (no timeout). Note: that happens (no wobj) because
    * in kcond_signal_int() we reset task's wobj before wakeing it up.
    *
    * In case `wobj.ptr` was != NULL, we woke up because of the timeout (which
    * doesn't reset the wobj), therefore return false.
    */

   ret = !wait_obj_reset(&curr->wobj);

   if (m) {
      kmutex_lock(m); // Re-acquire the lock [if any]
   }

   return ret;
}

static void
kcond_signal_int(struct kcond *c, struct wait_obj *wo)
{
   ASSERT(!is_preemption_enabled());
   DEBUG_ONLY(check_not_in_irq_handler());

   struct task *ti =
      wo->type != WOBJ_MWO_ELEM
         ? CONTAINER_OF(wo, struct task, wobj)
         : CONTAINER_OF(wo, struct mwobj_elem, wobj)->ti;

   if (ti->state != TASK_STATE_SLEEPING) {

      /* the signal is lost, that's typical for conditions */

      if (wo->type == WOBJ_MWO_ELEM)
         wait_obj_reset(wo);

      return;
   }

   if (wo->type != WOBJ_MWO_ELEM) {
      ASSERT(wo->type == WOBJ_KCOND);
      task_cancel_wakeup_timer(ti);
   } else {
      ASSERT(CONTAINER_OF(wo, struct mwobj_elem, wobj)->type == WOBJ_KCOND);
   }

   wait_obj_reset(wo);
   wake_up(ti);
}

void kcond_signal_one(struct kcond *c)
{
   disable_preemption();
   {
      DEBUG_ONLY(check_not_in_irq_handler());

      if (!list_is_empty(&c->wait_list)) {

         struct wait_obj *wobj =
            list_first_obj(&c->wait_list, struct wait_obj, wait_list_node);

         kcond_signal_int(c, wobj);
      }
   }
   enable_preemption();
}

void kcond_signal_all(struct kcond *c)
{
   struct wait_obj *wo_pos, *temp;
   disable_preemption();
   {
      DEBUG_ONLY(check_not_in_irq_handler());

      list_for_each(wo_pos, temp, &c->wait_list, wait_list_node) {
         kcond_signal_int(c, wo_pos);
      }
   }
   enable_preemption();
}

void kcond_destory(struct kcond *c)
{
   bzero(c, sizeof(struct kcond));
}
