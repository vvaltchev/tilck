/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/datetime.h>

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
is_waiting_on_multiple_children(struct task *ti, int *tid)
{
   struct wait_obj *wobj = &ti->wobj;

   if (ti->state != TASK_STATE_SLEEPING)
      return false;

   if (wobj->type != WOBJ_TASK)
      return false;

   *tid = (int)wait_obj_get_data(wobj);
   return *tid < 0;
}

static bool
is_good_reason_to_wake_up_task(struct wait_obj *wo, enum wakeup_reason r)
{
   return
      r == task_died                                                    ||
         (r == task_stopped && (wo->extra & WEXTRA_TASK_STOPPED))       ||
         (r == task_continued && (wo->extra & WEXTRA_TASK_CONTINUED));
}

void wake_up_tasks_waiting_on(struct task *ti, enum wakeup_reason r)
{
   struct wait_obj *wo, *wo_temp;
   struct process *pi = ti->pi;

   ASSERT(!is_preemption_enabled());

   list_for_each(wo, wo_temp, &ti->tasks_waiting_list, wait_list_node) {

      ASSERT(wo->type == WOBJ_TASK);
      struct task *task_to_wake_up = CONTAINER_OF(wo, struct task, wobj);

      if (task_to_wake_up->state != TASK_STATE_SLEEPING) {

         /*
          * The task MUST be in waitpid(), but it's not sleeping. Because of
          * the implementation of waitpid(), it's safe to just skip this task
          * here as it will notice the event before going to sleep again.
          */
         continue;
      }

      if (is_good_reason_to_wake_up_task(wo, r))
         wake_up(task_to_wake_up);
   }

   if (LIKELY(pi->parent_pid > 0)) {

      struct task *parent_task = get_task(pi->parent_pid);
      int tid;

      if (is_waiting_on_multiple_children(parent_task, &tid)      &&
          !waitpid_should_skip_child(parent_task, ti, tid)        &&
          is_good_reason_to_wake_up_task(&parent_task->wobj, r))
      {
         wake_up(parent_task);
      }

      send_signal(pi->parent_pid, SIGCHLD, true);
   }
}

/*
 * ***************************************************************
 *
 * SYSCALLS
 *
 * ***************************************************************
 */

int sys_wait4(int tid, int *user_wstatus, int options, void *user_rusage)
{
   struct task *curr = get_curr_task();
   struct task *chtask = NULL;
   int chtask_tid = -1;
   u16 wobj_extra = NO_EXTRA;

   if (options & WUNTRACED)
      wobj_extra |= WEXTRA_TASK_STOPPED;

   if (options & WCONTINUED)
      wobj_extra |= WEXTRA_TASK_CONTINUED;

   ASSERT(are_interrupts_enabled());

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

      /* No chtask has been found */

      if (!child_count) {
         /* No children to wait for */
         enable_preemption();
         return -ECHILD;
      }

      if (options & WNOHANG) {
         /* With WNOHANG we must not hang until a child changes state */
         enable_preemption();
         return 0;
      }

      /* Hang until a child changes state */
      prepare_to_wait_on(WOBJ_TASK, TO_PTR(tid), wobj_extra, wait_list);
      enter_sleep_wait_state();

      if (pending_signals())
         return -EINTR;

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

   if (user_rusage) {

      struct k_rusage ru = {0};
      struct k_timespec64 tp;
      u64 user_ticks = chtask->ticks.total - chtask->ticks.total_kernel;

      ticks_to_timespec(user_ticks, &tp);

      ru.ru_utime.tv_sec = (long) tp.tv_sec;
      ru.ru_utime.tv_usec = tp.tv_nsec / 1000;

      ticks_to_timespec(chtask->ticks.total_kernel, &tp);
      ru.ru_stime.tv_sec = (long) tp.tv_sec;
      ru.ru_stime.tv_usec = tp.tv_nsec / 1000;

      if (copy_to_user(user_rusage, &ru, sizeof(ru)) < 0)
         chtask_tid = -EFAULT;
   }

   if (chtask->state == TASK_STATE_ZOMBIE)
      remove_task(chtask);

   enable_preemption();
   return chtask_tid;
}

int sys_waitpid(int tid, int *user_wstatus, int options)
{
   return sys_wait4(tid, user_wstatus, options, NULL);
}
