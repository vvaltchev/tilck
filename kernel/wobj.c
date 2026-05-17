/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/sync.h>
#include <tilck/kernel/sched.h>

void wait_obj_set(struct wait_obj *wo,
                  enum wo_type type,
                  void *ptr,
                  u32 extra,
                  struct list *wait_list)
{
   STATIC_ASSERT(sizeof(wo->type) == sizeof(u32));
   ASSERT(type != WOBJ_NONE);

   disable_preemption();
   {
      /*
       * Caller must hand us a reset wobj. Checking via `type` works for
       * every wo_type, including those whose `ptr` may legitimately be
       * NULL (e.g. WOBJ_TASK with tid==0).
       */
      ASSERT(wo->type == WOBJ_NONE);
      ASSERT(list_node_is_null(&wo->wait_list_node) ||
             list_node_is_empty(&wo->wait_list_node));

      atomic_store(&wo->__ptr, ptr);
      wo->extra = extra;
      list_node_init(&wo->wait_list_node);

      if (wait_list)
         list_add_tail(wait_list, &wo->wait_list_node);

      /*
       * Publish: this store makes the wobj "live". A signaler
       * walking a wait_list and reading wo->type must see a fully
       * initialized wobj, so the type write goes last. wo->type
       * stays a plain enum (most readers access it directly under
       * preempt-disabled regions); the publish-store goes through
       * a volatile-qualified atomic builtin so the compiler can't
       * sink it past the field initializations above.
       */
      __atomic_store_n(
         (u32 volatile *)&wo->type,
         (u32)type,
         __ATOMIC_RELAXED
      );
   }
   enable_preemption();
}

void *wait_obj_reset(struct wait_obj *wo)
{
   enum wo_type old_type;
   void *oldp = NULL;

   STATIC_ASSERT(sizeof(wo->type) == sizeof(u32));

   disable_preemption();
   {
      /*
       * Atomic-exchange `type` to claim the cleanup: whoever swaps a
       * non-NONE value out is the one who tears the wobj down. The
       * exchange runs inside the preempt-disabled section so a
       * concurrent signaler iterating a wait_list never observes the
       * intermediate state (type==WOBJ_NONE while the node is still
       * linked) — which would corrupt the dispatch in kcond_signal_int.
       */
      old_type = (enum wo_type) __atomic_exchange_n(
         (u32 volatile *)&wo->type,
         (u32)WOBJ_NONE,
         __ATOMIC_RELAXED
      );

      if (old_type != WOBJ_NONE) {

         oldp = atomic_exchange(&wo->__ptr, NULL);

         if (list_is_node_in_list(&wo->wait_list_node))
            list_remove(&wo->wait_list_node);

         list_node_init(&wo->wait_list_node);
      }
   }
   enable_preemption();

   return oldp;
}

void prepare_to_wait_on(enum wo_type type,
                        void *ptr,
                        u32 extra,
                        struct list *wait_list)
{
   struct task *ti = get_curr_task();
   ASSERT(!is_preemption_enabled());

   if (UNLIKELY(in_panic())) {

      /*
       * Just set the wait object, don't change task's state.
       * See the comments in kcond_wait() for more context about that.
       */
      wait_obj_set(&ti->wobj, type, ptr, extra, wait_list);
      return;
   }

   ASSERT(atomic_load(&ti->state) != TASK_STATE_SLEEPING);
   wait_obj_set(&ti->wobj, type, ptr, extra, wait_list);
   task_change_state(ti, TASK_STATE_SLEEPING);
}

void *wake_up(struct task *ti)
{
   void *oldp;
   disable_preemption();
   {
      oldp = wait_obj_reset(&ti->wobj);

      if (ti != get_curr_task()) {

         /*
          * TODO: if SMP will be ever introduced, here we should call a
          * function that does NOT "downgrade" a task from RUNNING to RUNNABLE.
          * Until then, checking that ti != current is enough.
          */

         /*
          * Stop-on-wake: if a SIGSTOP-class signal arrived while
          * the task was SLEEPING, action_stop() set ti->stop_pending
          * but left the state SLEEPING (the wait_obj was untouched
          * on purpose). Honor that pending stop now by routing the
          * transition to STOPPED instead of RUNNABLE — the task
          * stays out of the runnable list/tree until SIGCONT. The
          * flag has done its job; consume it so the invariant
          * "stopped flag set => state == SLEEPING" is preserved.
          */
         enum task_state next = TASK_STATE_RUNNABLE;

         if (UNLIKELY(ti->stop_pending)) {
            next = TASK_STATE_STOPPED;
            ti->stop_pending = false;
         }

         task_change_state_idempotent(ti, next);
      }
   }
   enable_preemption();
   return oldp;
}

/* Multi wait obj stuff */

struct multi_obj_waiter *allocate_mobj_waiter(int elems)
{
   size_t s =
      sizeof(struct multi_obj_waiter) + sizeof(struct mwobj_elem) * (u32)elems;

   struct multi_obj_waiter *w = task_temp_kernel_alloc(s);

   if (!w)
      return NULL;

   bzero(w, s);
   w->count = elems;
   list_init(&w->signaled_list);

   /*
    * Put every elem's signaled_node in the "empty" state so cleanup paths
    * (e.g. free_mobj_waiter on a partially-initialized waiter) can safely
    * call list_is_node_in_list() on it without dereferencing NULL.
    */
   for (int i = 0; i < elems; i++)
      list_node_init(&w->elems[i].signaled_node);

   return w;
}

void free_mobj_waiter(struct multi_obj_waiter *w)
{
   if (!w)
      return;

   for (int i = 0; i < w->count; i++) {
      mobj_waiter_reset2(w, i);
   }

   task_temp_kernel_free(w);
}

void
mobj_waiter_set(struct multi_obj_waiter *w,
                int index,
                enum wo_type type,
                void *ptr,
                struct list *wait_list)
{
   /*
    * No chaining is allowed: the waited object pointed by `ptr` is expected to
    * be a regular (waitable) object like kcond.
    */
   ASSERT(type != WOBJ_MWO_WAITER && type != WOBJ_MWO_ELEM);

   struct mwobj_elem *e = &w->elems[index];

   /*
    * Populate the elem's state BEFORE wait_obj_set() links it into the
    * kcond wait_list: once linked, a signaler can find us at any moment
    * and will dereference e->waiter (to reach signaled_list) and read
    * e->saved_*. Setting these last would race with the first signal.
    */
   e->waiter = w;
   e->saved_ptr = ptr;
   e->saved_wait_list = wait_list;
   e->ti = get_curr_task();
   e->type = type;

   wait_obj_set(&e->wobj, WOBJ_MWO_ELEM, ptr, NO_EXTRA, wait_list);
}

void mobj_waiter_reset(struct mwobj_elem *e)
{
   wait_obj_reset(&e->wobj);

   /*
    * The elem may also be linked in waiter->signaled_list if a signal fired
    * since the last set/rearm. Unlink it here so this routine leaves the
    * elem in a fully clean state regardless of which "side" had it.
    */
   if (list_is_node_in_list(&e->signaled_node))
      list_remove(&e->signaled_node);
   list_node_init(&e->signaled_node);

   e->ti = NULL;
   e->waiter = NULL;
   e->saved_ptr = NULL;
   e->saved_wait_list = NULL;
   e->type = WOBJ_NONE;
}

void mobj_waiter_reset2(struct multi_obj_waiter *w, int index)
{
   struct mwobj_elem *e = &w->elems[index];
   mobj_waiter_reset(e);
}

/*
 * Re-attach every signaled elem to its original kcond wait_list.
 *
 * Used by poll()/select() when they wake up but the predicate re-check
 * finds nothing actually ready (e.g. a faster reader drained the data
 * between wake and check). Without this, the consumed elems would stay
 * deregistered and future signals on those kconds would miss us — the
 * poll/select would only wake again on the timer or on a *different*
 * kcond, even though the original one keeps firing.
 *
 * Caller must hold preemption disabled.
 */
void mobj_waiter_rearm_signaled(struct multi_obj_waiter *w)
{
   struct mwobj_elem *e, *temp;
   ASSERT(!is_preemption_enabled());

   list_for_each(e, temp, &w->signaled_list, signaled_node) {

      list_remove(&e->signaled_node);
      list_node_init(&e->signaled_node);

      wait_obj_set(&e->wobj, WOBJ_MWO_ELEM,
                   e->saved_ptr, NO_EXTRA, e->saved_wait_list);
   }
}

void prepare_to_wait_on_multi_obj(struct multi_obj_waiter *w)
{
   prepare_to_wait_on(WOBJ_MWO_WAITER, w, NO_EXTRA, NULL);
}
