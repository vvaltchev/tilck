
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/list.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/debug_utils.h>

//#define DEBUG_printk printk
#define DEBUG_printk(...)

#define EXITCODE(ret, sig)  ((ret) << 8 | (sig))
#define STOPCODE(sig) ((sig) << 8 | 0x7f)
#define CONTINUED 0xffff
#define COREFLAG 0x80

static bool do_common_task_allocations(task_info *ti)
{
   ti->kernel_stack = kzmalloc(KTHREAD_STACK_SIZE);

   if (!ti->kernel_stack)
      return false;

   ti->io_copybuf = kmalloc(IO_COPYBUF_SIZE + ARGS_COPYBUF_SIZE);

   if (!ti->io_copybuf) {
      kfree2(ti->kernel_stack, KTHREAD_STACK_SIZE);
      return false;
   }

   ti->args_copybuf = (void *)((uptr)ti->io_copybuf + IO_COPYBUF_SIZE);
   return true;
}

static void internal_free_mem_for_zombie_task(task_info *ti)
{
   kfree2(ti->io_copybuf, IO_COPYBUF_SIZE + ARGS_COPYBUF_SIZE);
   kfree2(ti->kernel_stack, KTHREAD_STACK_SIZE);

   ti->io_copybuf = NULL;
   ti->args_copybuf = NULL;
   ti->kernel_stack = NULL;
}

void free_mem_for_zombie_task(task_info *ti)
{
   ASSERT(ti->state == TASK_STATE_ZOMBIE);

#ifdef DEBUG

   uptr stack_var = 123;
   if (((uptr)&stack_var & PAGE_MASK) != (uptr)&kernel_initial_stack)
      panic("free_mem_for_zombie_task() called w/o switch to initial stack");

#endif

   internal_free_mem_for_zombie_task(ti);
}

task_info *allocate_new_process(task_info *parent, int pid)
{
   process_info *pi;
   task_info *ti = kmalloc(sizeof(task_info) + sizeof(process_info));

   if (!ti)
      return NULL;

   pi = (process_info *)(ti + 1);

   if (parent) {

      memcpy(ti, parent, sizeof(task_info));
      memcpy(pi, parent->pi, sizeof(process_info));
      pi->parent_pid = parent->tid;
      pi->mmap_heap = kmalloc_heap_dup(parent->pi->mmap_heap);

   } else {

      bzero(ti, sizeof(task_info) + sizeof(process_info));
      /* NOTE: parent_pid in this case is 0 as kernel_process->pi->tid */
   }

   if (!do_common_task_allocations(ti)) {
      kfree2(ti, sizeof(task_info) + sizeof(process_info));
      return NULL;
   }

   pi->ref_count = 1;
   ti->tid = pid; /* here tid is a PID */
   ti->owning_process_pid = pid;

   ti->pi = pi;
   bintree_node_init(&ti->tree_by_tid);
   list_node_init(&ti->runnable_list);
   list_node_init(&ti->sleeping_list);

   arch_specific_new_task_setup(ti);
   return ti;
}

task_info *allocate_new_thread(process_info *pi)
{
   task_info *proc = get_process_task(pi);
   task_info *ti = kzmalloc(sizeof(task_info));

   if (!ti || !do_common_task_allocations(ti)) {
      kfree2(ti, sizeof(task_info));
      return NULL;
   }

   ti->pi = pi;
   ti->tid = MAX_PID + (sptr)ti - KERNEL_BASE_VA;
   ti->owning_process_pid = proc->tid;

   arch_specific_new_task_setup(ti);
   return ti;
}

void free_task(task_info *ti)
{
   ASSERT(ti->state == TASK_STATE_ZOMBIE);
   arch_specific_free_task(ti);

   ASSERT(!ti->kernel_stack);
   ASSERT(!ti->io_copybuf);
   ASSERT(!ti->args_copybuf);

   if (ti->tid == ti->owning_process_pid) {

      ASSERT(ti->pi->ref_count > 0);

      if (ti->pi->mmap_heap) {
         kmalloc_destroy_heap(ti->pi->mmap_heap);
         kfree2(ti->pi->mmap_heap, kmalloc_get_heap_struct_size());
         ti->pi->mmap_heap = NULL;
      }

      if (--ti->pi->ref_count == 0)
         kfree2(ti, sizeof(task_info) + sizeof(process_info));

   } else {

      kfree2(ti, sizeof(task_info));
   }
}


/*
 * ***************************************************************
 *
 * SYSCALLS
 *
 * ***************************************************************
 */

sptr sys_pause()
{
   task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
   kernel_yield();
   return 0;
}

sptr sys_getpid()
{
   return get_curr_task()->owning_process_pid;
}

sptr sys_gettid()
{
   return get_curr_task()->tid;
}

void join_kernel_thread(int tid)
{
   ASSERT(is_preemption_enabled());

   task_info *ti = get_task(tid);

   if (!ti)
      return; /* the thread already exited */

   while (get_task(tid) != NULL) {
      wait_obj_set(&get_curr_task()->wobj, WOBJ_TASK, ti);
      task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
      kernel_yield();
   }
}

sptr sys_waitpid(int pid, int *wstatus, int options)
{
   ASSERT(are_interrupts_enabled());
   DEBUG_VALIDATE_STACK_PTR();

   if (pid > 0) {

      /* Wait for a specific PID */

      volatile task_info *waited_task = (volatile task_info *)get_task(pid);

      if (!waited_task) {
         return -ECHILD;
      }

      while (waited_task->state != TASK_STATE_ZOMBIE) {

         wait_obj_set(&get_curr_task()->wobj,
                      WOBJ_TASK,
                      (task_info *)waited_task);
         task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
         kernel_yield();
      }

      if (wstatus) {
         int value = EXITCODE(waited_task->exit_status, 0);

         if (copy_to_user(wstatus, &value, sizeof(int)) < 0) {
            remove_task((task_info *)waited_task);
            return -EFAULT;
         }
      }

      remove_task((task_info *)waited_task);
      return pid;

   } else {

      /*
       * Since Tilck does not support UIDs and GIDs != 0, the values of
       *    pid < -1
       *    pid == -1
       *    pid == 0
       * are treated in the same way.
       */

      NOT_IMPLEMENTED();
   }
}

sptr sys_wait4(int pid, int *wstatus, int options, void *user_rusage)

{
   char zero_buf[136] = {0};

   if (user_rusage) {
      // TODO: update when rusage is actually supported
      if (copy_to_user(user_rusage, zero_buf, sizeof(zero_buf)) < 0)
         return -EFAULT;
   }

   return sys_waitpid(pid, wstatus, options);
}

NORETURN sptr sys_exit(int exit_status)
{
   disable_preemption();
   task_info *curr = get_curr_task();

   // printk("Exit process %i with code = %i\n",
   //        current->pid,
   //        exit_status);

   task_change_state(curr, TASK_STATE_ZOMBIE);
   curr->exit_status = exit_status;

   // Close all of its opened handles

   for (size_t i = 0; i < ARRAY_SIZE(curr->pi->handles); i++) {

      fs_handle *h = curr->pi->handles[i];

      if (h) {
         vfs_close(h);
         curr->pi->handles[i] = NULL;
      }
   }


   // Wake-up all the tasks waiting on this task to exit

   task_info *pos;

   list_for_each(pos, &sleeping_tasks_list, sleeping_list) {

      // TODO: check the following failing ASSERT [fail-count: 1]
      // Debug stuff:
      //
      // Interrupts: [ 128 ]
      // Stacktrace (8 frames):
      // [0xc01017ce] dump_stacktrace + 0x30
      // [0xc010bd07] panic + 0x180
      // [0xc011e9c5] assert_failed + 0x19
      // [0xc010fa69] sys_exit + 0xb9
      // [0xc010fb56] sys_exit_group + 0x11
      // [0xc0107d96] handle_syscall + 0x136
      // [0xc010d2f2] soft_interrupt_entry + 0x7e
      // [0xc0101672] asm_soft_interrupt_entry + 0x37

      // ASSERT(pos->state == TASK_STATE_SLEEPING);

      if (pos->state != TASK_STATE_SLEEPING) {
         panic("%s task %d [w: %p] in the sleeping_tasks_list with state: %d",
               is_kernel_thread(pos) ? "kernel" : "user",
               pos->tid, pos->what, pos->state);
      }

      if (pos->wobj.ptr == curr) {
         ASSERT(pos->wobj.type == WOBJ_TASK);
         wait_obj_reset(&pos->wobj);
         task_change_state(pos, TASK_STATE_RUNNABLE);
      }
   }

   set_page_directory(get_kernel_pdir());
   pdir_destroy(curr->pi->pdir);

#ifdef DEBUG_QEMU_EXIT_ON_INIT_EXIT
   if (curr->tid == 1) {
      debug_qemu_turn_off_machine();
   }
#endif

   /* WARNING: the following call discards the whole stack! */
   switch_to_initial_kernel_stack();

   /* Free the heap allocations used by the task, including the kernel stack */
   free_mem_for_zombie_task(curr);
   schedule(X86_PC_TIMER_IRQ);

   /* Necessary to guarantee to the compiler that we won't return. */
   NOT_REACHED();
}

NORETURN sptr sys_exit_group(int status)
{
   // TODO: update when user threads are supported
   sys_exit(status);
}

// Returns child's pid
sptr sys_fork(void)
{
   disable_preemption();
   task_info *curr = get_curr_task();

   int pid = create_new_pid();

   if (pid == -1) {
      enable_preemption();
      return -EAGAIN;
   }

   task_info *child = allocate_new_process(curr, pid);

   if (!child) {
      enable_preemption();
      return -ENOMEM;
   }

   ASSERT(child->kernel_stack != NULL);

   if (child->state == TASK_STATE_RUNNING) {
      child->state = TASK_STATE_RUNNABLE;
   }


#if FORK_NO_COW
   child->pi->pdir = pdir_deep_clone(curr->pi->pdir);
#else
   child->pi->pdir = pdir_clone(curr->pi->pdir);
#endif

   if (!child->pi->pdir)
      goto no_mem_exit;

   child->running_in_kernel = false;
   task_info_reset_kernel_stack(child);

   child->state_regs--; // make room for a regs struct in child's stack
   *child->state_regs = *curr->state_regs; // copy parent's regs
   set_return_register(child->state_regs, 0);

   // Make the parent to get child's pid as return value.
   set_return_register(curr->state_regs, child->tid);

   /* Duplicate all the handles */
   for (u32 i = 0; i < ARRAY_SIZE(child->pi->handles); i++) {

      fs_handle h = child->pi->handles[i];

      if (!h)
         continue;

      fs_handle dup_h = NULL;
      int rc = vfs_dup(h, &dup_h);

      if (rc < 0 || !dup_h) {

         for (u32 j = 0; j < i; j++)
            vfs_close(child->pi->handles[j]);

         goto no_mem_exit;
      }

      child->pi->handles[i] = dup_h;
   }

   add_task(child);

   /*
    * Force the CR3 reflush using the current (parent's) pdir.
    * Without doing that, COW on parent's pages doesn't work immediately.
    * That is better (in this case) than invalidating all the pages affected,
    * one by one.
    */

   set_page_directory(curr->pi->pdir);

   enable_preemption();
   return child->tid;

no_mem_exit:
   child->state = TASK_STATE_ZOMBIE;
   internal_free_mem_for_zombie_task(child);
   free_task(child);
   enable_preemption();
   return -ENOMEM;
}


#include <sys/prctl.h>

sptr sys_prctl(int option, uptr a2, uptr a3, uptr a4, uptr a5)
{
   if (option == PR_SET_NAME) {
      // printk("[TID: %d] PR_SET_NAME '%s'\n", get_curr_task()->tid, a2);
      // TODO: save the task name in task_info.
      return 0;
   }

   printk("[TID: %d] Unknown option: %d\n", option);
   return -EINVAL;
}
