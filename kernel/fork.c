/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/process_mm.h>

static int fork_dup_all_handles(struct process *pi)
{
   ASSERT(!is_preemption_enabled());

   for (u32 i = 0; i < MAX_HANDLES; i++) {

      int rc;
      fs_handle dup_h = NULL;
      fs_handle h = pi->handles[i];
      struct user_mapping *um;

      if (!h)
         continue;

      rc = vfs_dup(h, &dup_h);

      if (rc < 0 || !dup_h) {

         enable_preemption();
         {
            for (u32 j = 0; j < i; j++)
               vfs_close(pi->handles[j]);
         }
         disable_preemption();
         return -ENOMEM;
      }

      /* Update file handle's process pointer to the new process */
      ((struct fs_handle_base *)dup_h)->pi = pi;

      /* Replace the older (parent's) handle with the new one */
      pi->handles[i] = dup_h;

      if (!pi->mi)
         continue;

      list_for_each_ro(um, &pi->mi->mappings, pi_node) {
         if (um->h == h)
            um->h = dup_h;
      }
   }

   return 0;
}

// Returns child's pid
int do_fork(bool vfork)
{
   int pid;
   int rc = -EAGAIN;
   struct task *child = NULL;
   struct task *curr = get_curr_task();
   struct process *curr_pi = curr->pi;
   pdir_t *new_pdir = NULL;

   disable_preemption();
   ASSERT(curr->state == TASK_STATE_RUNNING);

   if ((pid = create_new_pid()) < 0)
      goto out; /* NOTE: rc is already set to -EAGAIN */

   if (vfork) {

      if (!(child = allocate_new_process(curr, pid, curr_pi->pdir)))
         goto oom_case;

   } else {

      if (FORK_NO_COW)
         new_pdir = pdir_deep_clone(curr_pi->pdir);
      else
         new_pdir = pdir_clone(curr_pi->pdir);

      if (!new_pdir)
         goto oom_case;

      if (!(child = allocate_new_process(curr, pid, new_pdir)))
         goto oom_case;

      /* Set new_pdir in order to avoid its double-destruction */
      new_pdir = NULL;
   }

   /* Child's kernel stack must be set */
   ASSERT(child->kernel_stack != NULL);

   child->state = TASK_STATE_RUNNABLE;
   child->running_in_kernel = false;
   task_info_reset_kernel_stack(child);

   child->state_regs--; // make room for a regs_t struct in child's stack
   *child->state_regs = *curr->state_regs; // copy parent's regs_t
   set_return_register(child->state_regs, 0);

   // Make the parent to get child's pid as return value.
   set_return_register(curr->state_regs, (ulong) child->tid);

   if (fork_dup_all_handles(child->pi) < 0)
      goto oom_case;

   add_task(child);

   if (vfork) {

      curr->stopped = true;
      curr->vfork_stopped = true;

   } else {

      /*
       * X86-specific note:
       * Force the CR3 reflush using the current (parent's) pdir.
       * Without doing that, COW on parent's pages doesn't work immediately.
       * That is better than invalidating all the pages affected, one by one.
       */

      set_curr_pdir(curr_pi->pdir);
   }

   enable_preemption();

   if (vfork) {

      /* Yield indefinitely until the child dies or calls execve() */
      kernel_yield();

      /* Make absolutely sure that we're running because we're not stopped */
      ASSERT(!curr->stopped);
      ASSERT(!curr->vfork_stopped);

      /* Check that the child died or called execve() */
      ASSERT(
         (child->pi->did_call_execve && !child->pi->vforked) ||
         child->state == TASK_STATE_ZOMBIE
      );
   }

   return child->tid;


oom_case:

   rc = -ENOMEM;

   if (new_pdir)
      pdir_destroy(new_pdir);

   if (child) {
      child->state = TASK_STATE_ZOMBIE;
      free_common_task_allocs(child);
      free_task(child);
   }

out:
   enable_preemption();
   return rc;
}
