/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/debug_utils.h>

#include <sys/prctl.h>        // system header
#include <sys/wait.h>         // system header

static bool
waitpid_should_skip_child(struct task *waiting_task,
                          struct process *pos,
                          int pid)
{
   /*
    * pid has several special values, when not simply > 0:
    *
    *    < -1   meaning  wait for any child process whose process
    *           group ID is equal to the absolute value of pid.
    *
    *      -1   meaning wait for any child process.
    *
    *       0   meaning wait for any child process whose process
    *           group ID is equal to that of the calling process.
    */

   if (pid > 0)
      return pos->pid != pid;

   if (pid < -1) {

      /*
       * -pid is a process group id: skip children which don't belong to
       * that specific process group.
       */
      return pos->pgid != -pid;
   }

   if (pid == 0) {

      /* We have to skip children belonging to a different group */
      return pos->pgid != waiting_task->pi->pgid;
   }

   ASSERT(pid == -1);

   /* We're going to wait on any children */
   return false;
}

static struct task *
get_task_if_changed(struct task *ti, int opts)
{
   enum task_state s = atomic_load_explicit(&ti->state, mo_relaxed);

   if (s == TASK_STATE_ZOMBIE)
      return ti;

   return NULL;
}

static struct task *
get_child_with_changed_status(struct process *pi,
                              int pid,
                              int opts,
                              u32 *child_cnt_ref)
{
   struct task *curr = get_curr_task();
   struct task *chtask = NULL;
   struct process *pos;
   u32 cnt = 0;

   list_for_each_ro(pos, &pi->children, siblings_node) {

      if (waitpid_should_skip_child(curr, pos, pid))
         continue;

      cnt++;

      if ((chtask = get_task_if_changed(get_process_task(pos), opts)))
         break;
   }

   *child_cnt_ref = cnt;
   return chtask;
}

int sys_waitpid(int pid, int *user_wstatus, int options)
{
   struct task *curr = get_curr_task();
   struct task *chtask = NULL;
   int chtask_tid = -1;

   ASSERT(are_interrupts_enabled());
   DEBUG_VALIDATE_STACK_PTR();

   /*
    * TODO: make waitpid() able to wait on other child state changes, in
    * particular in case a children received a SIGSTOP or a SIGCONT.
    */

   while (true) {

      struct list *wait_list = NULL;
      u32 child_count = 0;

      disable_preemption();

      if (pid > 0) {

         struct task *waited_task = get_task(pid);

         if (!waited_task || waited_task->pi->parent_pid != curr->pi->pid) {
            enable_preemption();
            return -ECHILD;
         }

         wait_list = &waited_task->tasks_waiting_list;
         child_count = 1;

         chtask = get_task_if_changed(waited_task, options);

      } else {

         chtask = get_child_with_changed_status(curr->pi,
                                                pid,
                                                options,
                                                &child_count);
      }

      if (chtask) {
         chtask_tid = chtask->tid;
         break; /* note: leave the preemption disabled */
      }

      enable_preemption();

      /* No chtask has been found */

      if (options & WNOHANG) {
         /* With WNOHANG we must not hang until a child changes state */
         return 0;
      }

      if (!child_count) {
         /* No children to wait for */
         return -ECHILD;
      }

      /* Hang until a child changes state */
      task_set_wait_obj(curr, WOBJ_TASK, TO_PTR(pid), wait_list);
      kernel_yield();

   } // while (true)

   /*
    * The only way to get here is a positive branch in `if (chtask)`: this mean
    * that we have a valid `chtask` and that preemption is disabled.
    */
   ASSERT(!is_preemption_enabled());

   if (user_wstatus) {
      if (copy_to_user(user_wstatus, &chtask->exit_wstatus, sizeof(s32)) < 0)
         chtask_tid = -EFAULT;
   }

   if (chtask->state == TASK_STATE_ZOMBIE)
      remove_task(chtask);

   enable_preemption();
   return chtask_tid;
}

int sys_wait4(int pid, int *user_wstatus, int options, void *user_rusage)
{
   struct k_rusage ru = {0};

   if (user_rusage) {
      // TODO: update when rusage is actually supported
      if (copy_to_user(user_rusage, &ru, sizeof(ru)) < 0)
         return -EFAULT;
   }

   return sys_waitpid(pid, user_wstatus, options);
}

static bool
task_is_waiting_on_multiple_children(struct task *ti, int *pid)
{
   struct wait_obj *wobj = &ti->wobj;

   if (ti->state != TASK_STATE_SLEEPING)
      return false;

   if (wobj->type != WOBJ_TASK)
      return false;

   *pid = (int)(sptr)wait_obj_get_ptr(wobj);
   return *pid < 0;
}

void wake_up_tasks_waiting_on(struct task *ti)
{
   struct wait_obj *wo_pos, *wo_temp;
   struct process *pi = ti->pi;

   ASSERT(!is_preemption_enabled());

   list_for_each(wo_pos, wo_temp, &ti->tasks_waiting_list, wait_list_node) {

      ASSERT(wo_pos->type == WOBJ_TASK);

      struct task *task_to_wake_up = CONTAINER_OF(
         wo_pos, struct task, wobj
      );
      task_reset_wait_obj(task_to_wake_up);
   }

   if (LIKELY(pi->parent_pid > 0)) {

      struct task *parent_task = get_task(pi->parent_pid);
      int pid;

      if (task_is_waiting_on_multiple_children(parent_task, &pid))
         if (!waitpid_should_skip_child(parent_task, ti->pi, pid))
            task_reset_wait_obj(parent_task);
   }
}
