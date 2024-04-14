/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_tracing.h>

#include <tilck_gen_headers/config_debug.h>
#include <tilck/common/basic_defs.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/debug_utils.h>

#include <tilck/mods/tracing.h>

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
      kfree_obj(alloc, struct kernel_alloc);
   }
}

static void init_terminated(struct task *ti, int exit_code, int term_sig)
{
   if (DEBUG_QEMU_EXIT_ON_INIT_EXIT)
      debug_qemu_turn_off_machine();

   if (!term_sig)
      panic("Init exited with code: %d\n", exit_code);
   else
      panic("Init terminated by signal %s[%d]\n",
            get_signal_name(term_sig), term_sig);
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

      if (pi->sa_handlers[SIGCHLD - 1] == SIG_IGN)
         pos->pi->automatic_reaping = true;

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

static void
close_all_handles(void)
{
   struct process *pi = get_curr_proc();
   ASSERT(is_preemption_enabled());

   for (int i = 0; i < MAX_HANDLES; i++) {
      if (pi->handles[i]) {
         vfs_close(pi->handles[i]);
         pi->handles[i] = NULL;
      }
   }
}

struct on_task_exit_cb {
   struct list_node node;
   void (*cb)(struct task *);
};

int
register_on_task_exit_cb(void (*cb)(struct task *))
{
   struct task *ti = get_curr_task();
   int rc = 0;

   disable_preemption();
   {
      struct on_task_exit_cb *obj = kalloc_obj(struct on_task_exit_cb);

      if (obj) {

         list_node_init(&obj->node);
         list_add_tail(&ti->on_exit, &obj->node);
         obj->cb = cb;

      } else {

         rc = -ENOMEM;
      }
   }
   enable_preemption();
   return rc;
}

int
unregister_on_task_exit_cb(void (*cb)(struct task *))
{
   struct task *ti = get_curr_task();
   struct on_task_exit_cb *pos, *tmp;
   int rc = -ENOENT;

   disable_preemption();
   {
      list_for_each(pos, tmp, &ti->on_exit, node) {
         if (pos->cb == cb) {
            list_remove(&pos->node);
            rc = 0;
            break;
         }
      }
   }
   enable_preemption();
   return rc;
}

static void
call_on_task_exit_callbacks(void)
{
   struct task *ti = get_curr_task();
   struct on_task_exit_cb *pos, *tmp;

   list_for_each(pos, tmp, &ti->on_exit, node) {
      pos->cb(ti);
      kfree_obj(pos, struct on_task_exit_cb);
   }
}

/*
 * Note: we HAVE TO make this function NO_INLINE otherwise clang in release
 * builds generates code that is incompatible with asm hacks changing both
 * the stack pointer and the frame pointer. It is worth mentioning that even
 * copying the whole stack to a new place is still not enough for clang.
 * Therefore, the simplest and reliable thing we can do is just to make the
 * following function be non-inlineable and take no arguments.
 */
NORETURN static NO_INLINE void
switch_stack_and_reschedule(void)
{
   /* WARNING: DO NOT USE ANY STACK VARIABLES HERE */
   ASSERT_CURR_TASK_STATE(TASK_STATE_ZOMBIE);
   ASSERT(!is_preemption_enabled());

   // XXX: tmp
   bzero(kernel_initial_stack, KERNEL_STACK_SIZE);

   /* WARNING: the following call discards the whole stack! */
   switch_to_initial_kernel_stack();

   /* Run the scheduler */
   do_schedule();

   /* Reassure the compiler that we won't return */
   NOT_REACHED();
}


/*
 * NOTE: this code ASSUMES that user processes have a _single_ thread:
 *
 *    process = task = thread
 *
 * TODO: re-design this function when thread support is introduced.
 *
 * NOTE: the kernel "process" has multiple threads (kthreads), but they cannot
 * be signalled nor killed.
 */
void terminate_process(int exit_code, int term_sig)
{
   struct task *const ti = get_curr_task();
   struct process *const pi = ti->pi;
   struct task *parent;
   const bool vforked = pi->vforked;

   ASSERT(ti->state != TASK_STATE_ZOMBIE);
   ASSERT(!is_kernel_thread(ti));
   ASSERT(is_preemption_enabled());

   if (term_sig)
      trace_task_killed(term_sig);

   disable_preemption();

   if (ti->wobj.type != WOBJ_NONE) {

      /*
       * If the task has been waiting on something, we have to reset its wobj
       * and remove its pointer from the target object's wait_list.
       */

      wait_obj_reset(&ti->wobj);
   }

   /*
    * Sleep-based wake-up timers work without the wait_obj mechanism: we have
    * to cancel any potential wake-up timer as well.
    */
   task_cancel_wakeup_timer(ti);

   /* Here we can either be RUNNABLE (if ti->wobj was set) or RUNNING */
   ASSERT(ti->state == TASK_STATE_RUNNING || ti->state == TASK_STATE_RUNNABLE);

   /* Drop the any pending signals and prevent new from being enqueued */
   drop_all_pending_signals(ti);
   ti->nested_sig_handlers = -1;

   /*
    * Close all the handles, keeping the preemption enabled while doing so.
    */
   enable_preemption();
   {
      close_all_handles();
   }
   disable_preemption();

   /*
    * OK, from now on the preemption won't be enabled until we switch to
    * a different task.
    */
   task_change_state(ti, TASK_STATE_ZOMBIE);
   ti->wstatus = EXITCODE(exit_code, term_sig);
   parent = get_task(pi->parent_pid);

   call_on_task_exit_callbacks();
   task_free_all_kernel_allocs(ti);

   if (vforked) {

      /*
       * In case of vforked processes, we cannot remove any mappings and we
       * need some special management for the mappings info object (pi->mi).
       */
      vforked_child_transfer_dispose_mi(pi);

   }  else {

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
      unblock_parent_of_vforked_child(pi);
   }

   if (parent) {
      if (parent->pi->sa_handlers[SIGCHLD - 1] == SIG_IGN)
         pi->automatic_reaping = true;
   }

   set_curr_pdir(get_kernel_pdir());

   if (!vforked)
      pdir_destroy(pi->pdir);

   switch_stack_and_reschedule();
}
