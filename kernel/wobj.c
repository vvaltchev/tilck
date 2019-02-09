/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/atomics.h>

#include <tilck/kernel/sync.h>
#include <tilck/kernel/process.h>

void wait_obj_set(wait_obj *wo,
                  enum wo_type type,
                  void *ptr,
                  list *wait_list)
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

void *wait_obj_reset(wait_obj *wo)
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

void task_set_wait_obj(task_info *ti,
                       enum wo_type type,
                       void *ptr,
                       list *wait_list)
{
   wait_obj_set(&ti->wobj, type, ptr, wait_list);
   ASSERT(ti->state != TASK_STATE_SLEEPING);
   task_change_state(ti, TASK_STATE_SLEEPING);
}

void *task_reset_wait_obj(struct task_info *ti)
{
   void *oldp = wait_obj_reset(&ti->wobj);
   ASSERT(ti->state == TASK_STATE_SLEEPING);
   task_change_state(ti, TASK_STATE_RUNNABLE);
   return oldp;
}

/* Multi wait obj stuff */

multi_obj_waiter *allocate_mobj_waiter(u32 elems)
{
   size_t s = sizeof(multi_obj_waiter) + sizeof(mwobj_elem) * elems;
   multi_obj_waiter *w = kzmalloc(s);

   if (!w)
      return NULL;

   w->count = elems;
   return w;
}

void free_mobj_waiter(multi_obj_waiter *w)
{
   if (!w)
      return;

   size_t s = sizeof(multi_obj_waiter) + sizeof(mwobj_elem) * w->count;
   kfree2(w, s);
}

void
mobj_waiter_set(multi_obj_waiter *w,
                u32 index,
                enum wo_type type,
                void *ptr,
                list *wait_list)
{
   /*
    * No chaining is allowed: the waited object pointed by `ptr` is expected to
    * be a regular (waitable) object like kcond.
    */
   ASSERT(type != WOBJ_MWO_WAITER && type != WOBJ_MWO_ELEM);

   mwobj_elem *e = &w->elems[index];
   wait_obj_set(&e->wobj, type, ptr, wait_list);
   e->ti = get_curr_task();
   e->type = type;
}

void mobj_waiter_reset(multi_obj_waiter *w, u32 index)
{
   mwobj_elem *e = &w->elems[index];
   wait_obj_reset(&e->wobj);
   e->ti = NULL;
   e->type = WOBJ_NONE;
}
