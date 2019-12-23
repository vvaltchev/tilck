/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/debug_utils.h>

#include <sys/prctl.h>        // system header
#include <sys/wait.h>         // system header

static bool
waitpid_should_skip_child(struct task *waiting_task,
                          struct task *pos,
                          int tid)
{
   /*
    * tid has several special values:
    *
    *     > 0   meaning wait for the child whose tid is equal to `tid`.
    *
    *    < -1   meaning  wait for any child process whose process
    *           group ID is equal to the absolute value of pid.
    *
    *      -1   meaning wait for any child process.
    *
    *       0   meaning wait for any child process whose process
    *           group ID is equal to that of the calling process.
    *
    * NOTE: Tilck's tid is called `pid` in the Linux kernel. What Tilck calls
    * `pid` is called in Linux `tgid` (thread ground id).
    */

   if (tid > 0)
      return pos->tid != tid;

   if (tid < -1) {

      /*
       * -pid is a process group id: skip children which don't belong to
       * that specific process group.
       */
      return pos->pi->pgid != -tid;
   }

   if (tid == 0) {

      /* We have to skip children belonging to a different group */
      return pos->pi->pgid != waiting_task->pi->pgid;
   }

   ASSERT(tid == -1);

   /* We're going to wait on any children */
   return false;
}

static struct task *
get_task_if_changed(struct task *ti, int opts)
{
   enum task_state s = atomic_load_explicit(&ti->state, mo_relaxed);

   if (s == TASK_STATE_ZOMBIE)
      return ti;

   if (ti->stopped && !ti->was_stopped && (opts & WUNTRACED)) {
      ti->was_stopped = true;
      return ti;
   }

   if (!ti->stopped && ti->was_stopped && (opts & WCONTINUED)) {
      ti->was_stopped = false;
      return ti;
   }

   return NULL;
}

static struct task *
get_child_with_changed_status(struct process *pi,
                              int tid,
                              int opts,
                              u32 *child_cnt_ref)
{
   struct task *curr = get_curr_task();
   struct task *chtask = NULL;
   struct task *pos;
   u32 cnt = 0;

   list_for_each_ro(pos, &pi->children, siblings_node) {

      if (waitpid_should_skip_child(curr, pos, tid))
         continue;

      cnt++;

      if ((chtask = get_task_if_changed(pos, opts)))
         break;
   }

   *child_cnt_ref = cnt;
   return chtask;
}

static bool
task_is_waiting_on_multiple_children(struct task *ti, int *tid)
{
   struct wait_obj *wobj = &ti->wobj;

   if (ti->state != TASK_STATE_SLEEPING)
      return false;

   if (wobj->type != WOBJ_TASK)
      return false;

   *tid = (int)(sptr)wait_obj_get_ptr(wobj);
   return *tid < 0;
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
      int tid;

      if (task_is_waiting_on_multiple_children(parent_task, &tid))
         if (!waitpid_should_skip_child(parent_task, ti, tid))
            task_reset_wait_obj(parent_task);
   }
}

static inline bool
task_is_parent(struct task *parent, struct task *child)
{
   return child->pi->parent_pid == parent->pi->pid;
}

/*
 * ***************************************************************
 *
 * SYSCALLS
 *
 * ***************************************************************
 */

int sys_waitpid(int tid, int *user_wstatus, int options)
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

      if (tid > 0) {

         struct task *waited_task = get_task(tid);

         if (!waited_task || !task_is_parent(curr, waited_task)) {
            enable_preemption();
            return -ECHILD;
         }

         wait_list = &waited_task->tasks_waiting_list;
         child_count = 1;

         chtask = get_task_if_changed(waited_task, options);

      } else {

         chtask = get_child_with_changed_status(curr->pi,
                                                tid,
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
      task_set_wait_obj(curr, WOBJ_TASK, TO_PTR(tid), wait_list);
      kernel_yield();

   } // while (true)

   /*
    * The only way to get here is a positive branch in `if (chtask)`: this mean
    * that we have a valid `chtask` and that preemption is disabled.
    */
   ASSERT(!is_preemption_enabled());

   if (user_wstatus) {
      if (copy_to_user(user_wstatus, &chtask->wstatus, sizeof(s32)) < 0)
         chtask_tid = -EFAULT;
   }

   if (chtask->state == TASK_STATE_ZOMBIE)
      remove_task(chtask);

   enable_preemption();
   return chtask_tid;
}

int sys_wait4(int tid, int *user_wstatus, int options, void *user_rusage)
{
   struct k_rusage ru = {0};

   if (user_rusage) {
      // TODO: update when rusage is actually supported
      if (copy_to_user(user_rusage, &ru, sizeof(ru)) < 0)
         return -EFAULT;
   }

   return sys_waitpid(tid, user_wstatus, options);
}
