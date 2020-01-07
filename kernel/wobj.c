/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/sync.h>
#include <tilck/kernel/process.h>

void wait_obj_set(struct wait_obj *wo,
                  enum wo_type type,
                  void *ptr,
                  struct list *wait_list)
{
   atomic_store_explicit(&wo->__ptr, ptr, mo_relaxed);

   disable_preemption();
   {
      ASSERT(list_node_is_null(&wo->wait_list_node) ||
             list_node_is_empty(&wo->wait_list_node));

      wo->type = type;
      list_node_init(&wo->wait_list_node);

      if (wait_list)
         list_add_tail(wait_list, &wo->wait_list_node);
   }
   enable_preemption();
}

void *wait_obj_reset(struct wait_obj *wo)
{
   void *oldp = atomic_exchange_explicit(&wo->__ptr, (void*)NULL, mo_relaxed);
   disable_preemption();
   {
      wo->type = WOBJ_NONE;

      if (list_is_node_in_list(&wo->wait_list_node)) {
         list_remove(&wo->wait_list_node);
      }

      list_node_init(&wo->wait_list_node);
   }
   enable_preemption();
   return oldp;
}

void task_set_wait_obj(struct task *ti,
                       enum wo_type type,
                       void *ptr,
                       u16 extra,
                       struct list *wait_list)
{
   disable_preemption();
   {
      wait_obj_set(&ti->wobj, type, ptr, wait_list);
      ti->wobj.extra = extra;
      ASSERT(ti->state != TASK_STATE_SLEEPING);
      task_change_state(ti, TASK_STATE_SLEEPING);
   }
   enable_preemption();
}

void *task_reset_wait_obj(struct task *ti)
{
   void *oldp;
   disable_preemption();
   {
      oldp = wait_obj_reset(&ti->wobj);
      ASSERT(ti->state == TASK_STATE_SLEEPING);
      task_change_state(ti, TASK_STATE_RUNNABLE);
   }
   enable_preemption();
   return oldp;
}

/* Multi wait obj stuff */

struct multi_obj_waiter *allocate_mobj_waiter(u32 elems)
{
   size_t s =
      sizeof(struct multi_obj_waiter) + sizeof(struct mwobj_elem) * elems;

   struct multi_obj_waiter *w = task_temp_kernel_alloc(s);

   if (!w)
      return NULL;

   bzero(w, s);
   w->count = elems;
   return w;
}

void free_mobj_waiter(struct multi_obj_waiter *w)
{
   if (!w)
      return;

   for (u32 i = 0; i < w->count; i++) {
      mobj_waiter_reset2(w, i);
   }

   task_temp_kernel_free(w);
}

void
mobj_waiter_set(struct multi_obj_waiter *w,
                u32 index,
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
   wait_obj_set(&e->wobj, WOBJ_MWO_ELEM, ptr, wait_list);
   e->ti = get_curr_task();
   e->type = type;
}

void mobj_waiter_reset(struct mwobj_elem *e)
{
   wait_obj_reset(&e->wobj);
   e->ti = NULL;
   e->type = WOBJ_NONE;
}

void mobj_waiter_reset2(struct multi_obj_waiter *w, u32 index)
{
   struct mwobj_elem *e = &w->elems[index];
   mobj_waiter_reset(e);
}

void kernel_sleep_on_waiter(struct multi_obj_waiter *w)
{
   task_set_wait_obj(get_curr_task(), WOBJ_MWO_WAITER, w, NO_EXTRA, NULL);
   kernel_yield();
}
