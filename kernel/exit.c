/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/debug_utils.h>

static void
task_free_all_kernel_allocs(struct task *ti)
{
   ASSERT(!is_preemption_enabled());

   while (ti->kallocs_tree_root != NULL) {

      /* Save a pointer to the alloc object on the stack */
      struct kernel_alloc *alloc = ti->kallocs_tree_root;

      /* Free the allocated chunk */
      kfree2(alloc->vaddr, alloc->size);

      /* Remove the kernel_alloc elem from the tree */
      bintree_remove_ptr(&ti->kallocs_tree_root,
                         alloc,
                         struct kernel_alloc,
                         node,
                         vaddr);

      /* Free the kernel_alloc object itself */
      kfree2(alloc, sizeof(struct kernel_alloc));
   }
}

static void init_terminated(struct task *ti, int exit_code, int term_sig)
{
   if (DEBUG_QEMU_EXIT_ON_INIT_EXIT)
      debug_qemu_turn_off_machine();

   if (!term_sig)
      panic("Init exited with code: %d\n", exit_code);
   else
      panic("Init terminated by signal %d\n", term_sig);
}

static struct process *get_child_reaper(struct process *pi)
{
   /* TODO: support prctl(PR_SET_CHILD_SUBREAPER) */
   ASSERT(!is_preemption_enabled());

   struct task *child_reaper = get_task(1); /* init */
   VERIFY(child_reaper != NULL);

   return child_reaper->pi;
}

static void
handle_children_of_dying_process(struct task *ti)
{
   struct process *pi = ti->pi;
   struct task *pos, *temp;
   struct process *child_reaper = get_child_reaper(pi);

   list_for_each(pos, temp, &pi->children, siblings_node) {

      list_remove(&pos->siblings_node);
      list_add_tail(&child_reaper->children, &pos->siblings_node);
      pos->pi->parent_pid = child_reaper->pid;

      if (pos->state == TASK_STATE_ZOMBIE) {

         /*
          * Corner case: the dying task had already dead children which it
          * did not wait for. Their exit code couldn't be retrieved by the
          * nearest reaper (init) because their parent was still alive when
          * they died. But now, they also have to be waited by the nearest
          * reaper, along with their parent.
          */

         wake_up_tasks_waiting_on(pos, task_died);
      }
   }
}

static void close_all_handles(struct process *pi)
{
   fs_handle *h;

   for (u32 i = 0; i < MAX_HANDLES; i++) {

      if (!(h = pi->handles[i]))
         continue;

      vfs_close2(pi, h);
      pi->handles[i] = NULL;
   }
}

/*
 * Note: we HAVE TO make this function NO_INLINE otherwise clang in release
 * builds generates code that is incompatible with asm hacks chaining both
 * the stack pointer and the frame pointer. It is worth mentioning that even
 * copying the whole stack to a new place is still not enough for clang.
 * Therefore, the simplest and reliable thing we can do is just to make the
 * following function be non-inlineable and take no arguments.
 */
static NORETURN NO_INLINE void
switch_stack_free_mem_and_schedule(void)
{
   ASSERT(get_curr_task()->state == TASK_STATE_ZOMBIE);

   /* WARNING: the following call discards the whole stack! */
   switch_to_initial_kernel_stack();

   /* Free the heap allocations used, including the kernel stack */
   free_mem_for_zombie_task(get_curr_task());

   /* Run the scheduler */
   schedule_outside_interrupt_context();

   /* Reassure the compiler that we won't return (schedule() is not NORETURN) */
   NOT_REACHED();
}


/*
 * NOTE: this code ASSUMES that user processes have a _single_ thread:
 *
 *    process = task = thread
 *
 * TODO: re-design/adapt this function when thread support is introduced.
 *
 * NOTE: the kernel "process" has multiple threads (kthreads), but they cannot
 * be signalled nor killed.
 */
void terminate_process(struct task *ti, int exit_code, int term_sig)
{
   ASSERT(!is_preemption_enabled());
   ASSERT(!is_kernel_thread(ti));

   struct process *const pi = ti->pi;
   const bool vforked = pi->vforked;

   if (ti->state == TASK_STATE_ZOMBIE)
      return; /* do nothing, the task is already dead */

   if (ti->wobj.type != WOBJ_NONE) {

      /*
       * If the task has been waiting on something, we have to reset its wobj
       * and remove its pointer from the target object's wait_list.
       *
       * NOTE: change first task's state to SLEEPING just to make happy
       * task_reset_wait_obj(), which expects the process to be sleeping when
       * it's called. In our case, because there's a wait obj set, this task
       * have been forcibly awaken to die by a signal.
       */

      task_change_state(ti, TASK_STATE_SLEEPING);
      task_reset_wait_obj(ti);
   }

   /*
    * Sleep-based wake-up timers work without the wait_obj mechanism: we have
    * to cancel any potential wake-up timer.
    */
   task_cancel_wakeup_timer(ti);
   task_change_state(ti, TASK_STATE_ZOMBIE);
   ti->wstatus = EXITCODE(exit_code, term_sig);

   close_all_handles(pi);
   task_free_all_kernel_allocs(ti);

   if (!vforked) {

      remove_all_user_zero_mem_mappings(pi);

      if (pi->elf)
         release_subsys_flock(pi->elf);
   }

   if (LIKELY(ti->tid != 1)) {

      /*
       * What if the dying task has any children? We have to set their parent
       * to init (pid 1) or to the nearest child subreaper, once that is
       * supported.
       */

      handle_children_of_dying_process(ti);

   } else {

      /* The dying task is PID 1, init */
      init_terminated(ti, exit_code, term_sig);
   }

   /* Wake-up all the tasks waiting on this specific task to exit */
   wake_up_tasks_waiting_on(ti, task_died);

   if (term_sig) {

      /*
       * The process has been killed. It makes sense to perform some additional
       * actions, in order to preserve the system in a healty state.
       */

      if (pi->did_set_tty_medium_raw)
         tty_set_medium_raw_mode(pi->proc_tty, false);
   }

   if (vforked) {
      handle_vforked_child_move_on(pi);
      pi->vforked = true; /* handle_vforked_child_move_on() unsets this */

      if (!pi->inherited_mmap_heap) {

         struct task *parent = get_task(pi->parent_pid);

         /* We're in a vfork-ed child: the parent cannot die */
         ASSERT(parent != NULL);

         /*
          * If we didn't inherit mappings info from the parent and the parent
          * didn't run the whole time: its `mi` must continue to be NULL.
          */
         ASSERT(!parent->pi->mi);

         /* Transfer the ownership of our mappings info back to our parent */
         parent->pi->mi = pi->mi;
      }
   }

   if (ti == get_curr_task()) {

      /* This function has been called by sys_exit(): we won't return */
      set_curr_pdir(get_kernel_pdir());

      if (!vforked)
         pdir_destroy(pi->pdir);

      switch_stack_free_mem_and_schedule();
      NOT_REACHED();
   }

   if (!vforked)
      pdir_destroy(pi->pdir);

   free_mem_for_zombie_task(ti);
}
