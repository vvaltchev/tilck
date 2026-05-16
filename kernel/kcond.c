/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>

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

   /*
    * On a recursive mutex held more than once, the single kmutex_unlock(m)
    * below only decrements lock_count and keeps the mutex held — the
    * signaler can't acquire m to update the predicate, and we'd deadlock.
    * Reject the configuration up front (POSIX leaves it undefined too).
    */
   ASSERT(!m || !(m->flags & KMUTEX_FL_RECURSIVE) || m->lock_count == 1);

   struct task *curr = get_curr_task();
   bool ret;

panic_retry_hack:

   disable_preemption();
   prepare_to_wait_on(WOBJ_KCOND, c, NO_EXTRA, &c->wait_list);

   if (timeout_ticks != KCOND_WAIT_FOREVER)
      task_set_wakeup_timer(curr, timeout_ticks);

   if (m) {
      kmutex_unlock(m);
   }

   if (UNLIKELY(in_panic())) {

      /*
       * During panic, everything is special. When kopt_panic_kb is enabled,
       * there's a single task, the current one, and IRQ 1 is enabled too.
       * Waiting for an event means just waiting for an IRQ. Halting the CPU
       * is what sleeping means in the special single-task mode we have in
       * panic, when kopt_panic_kb is enabled.
       */
      halt();
      enable_preemption();

   } else {

      /* Go to sleep until a signal is fired or timeout happens */
      enter_sleep_wait_state();
   }

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

   if (UNLIKELY(in_panic())) {

      /*
       * When we're in panic and kopt_panic_kb is enabled, we have just a single
       * task, no scheduler and no IRQs other than from PS/2 or COM1. Still,
       * the TTY layer needs condition variables to work in order to canonical
       * mode to work. We cannot sleep in the proper sense, by we can HALT the
       * the CPU and wait for an IRQ to wake us up. Now, the problem is that we
       * will wake up on every IRQ, even if we never got signalled. That would
       * incorrect behavior from the caller point of view therefore, we need
       * to check if the wait_obj_reset() succeeded: in the positive case, we
       * know that's not a timeout here (because signal resets the wobj for us),
       * so we retry the whole thing. If it didn't succeed, we have been
       * signalled, so return. In other words, kcond_wait() returns ALWAYS true
       * during panic.
       */

      if (!ret)
         goto panic_retry_hack;
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

   if (UNLIKELY(in_panic())) {

      /*
       * In this case, we have to ignore the task's state, which cannot change
       * and just reset the wait object, in order to kcond_wait() to understand
       * that this condition has been actually signalled.
       *
       * See the comments above in kcond_wait() for more context.
       */
      wait_obj_reset(wo);
      return;
   }

   if (wo->type == WOBJ_MWO_ELEM) {

      /*
       * Multi-obj path (poll/select). Always remove the elem from c->wait_list
       * via wait_obj_reset() and stash it in waiter->signaled_list, so the
       * wakee can later identify which kcond(s) fired in O(num_signaled)
       * instead of scanning the whole elems[] array.
       *
       * Recording the signal is unconditional: even if `ti` is no longer
       * SLEEPING (e.g. the timer already woke it, or a previous signal did),
       * the waiter may choose to re-sleep when it discovers nothing is ready
       * — in that case mobj_waiter_rearm_signaled() will drain signaled_list
       * and re-attach the elems. wake_up() itself is only called when the
       * task is still SLEEPING, since otherwise it's a no-op anyway.
       */

      struct mwobj_elem *e = CONTAINER_OF(wo, struct mwobj_elem, wobj);
      ASSERT(e->type == WOBJ_KCOND);

      wait_obj_reset(wo);
      list_add_tail(&e->waiter->signaled_list, &e->signaled_node);

      if (ti && ti->state == TASK_STATE_SLEEPING)
         wake_up(ti);

      return;
   }

   /* Single-obj path: traditional kcond wait. */
   if (!ti || ti->state != TASK_STATE_SLEEPING) {
      /* the signal is lost, that's typical for conditions */
      return;
   }

   ASSERT(wo->type == WOBJ_KCOND);
   task_cancel_wakeup_timer(ti);
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

void kcond_destroy(struct kcond *c)
{
   ASSERT(list_is_empty(&c->wait_list));
   bzero(c, sizeof(struct kcond));
}
