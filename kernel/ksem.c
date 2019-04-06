/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/sync.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/sched.h>

void ksem_init(ksem *s)
{
   s->counter = 1;
   list_init(&s->wait_list);
}

void ksem_destroy(ksem *s)
{
   bzero(s, sizeof(ksem));
}

void ksem_wait(ksem *s)
{
   disable_preemption();

   if (--s->counter < 0) {

      task_set_wait_obj(get_curr_task(), WOBJ_SEM, s, &s->wait_list);
      enable_preemption();
      kernel_yield();
      return;
   }

   enable_preemption();
}

void ksem_signal(ksem *s)
{
   disable_preemption();

   if (s->counter++ < 0) {

      ASSERT(!list_is_empty(&s->wait_list));

      wait_obj *task_wo =
         list_first_obj(&s->wait_list, wait_obj, wait_list_node);

      task_info *ti = CONTAINER_OF(task_wo, task_info, wobj);

      ASSERT(ti->state == TASK_STATE_SLEEPING);
      task_reset_wait_obj(ti);
   }

   enable_preemption();
}
