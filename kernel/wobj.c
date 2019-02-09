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
