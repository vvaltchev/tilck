/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/list.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/debug_utils.h>

#include <sys/prctl.h> // system header
#include <sys/wait.h>  // system header

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

   /* The first process (init) is has as parent 'kernel_process' */
   ASSERT(parent != NULL);

   memcpy(ti, parent, sizeof(task_info));
   memcpy(pi, parent->pi, sizeof(process_info));
   pi->parent_pid = parent->tid;
   pi->mmap_heap = kmalloc_heap_dup(parent->pi->mmap_heap);

   if (!do_common_task_allocations(ti)) {
      kfree2(ti, sizeof(task_info) + sizeof(process_info));
      return NULL;
   }

   pi->ref_count = 1;
   ti->tid = pid; /* here tid is a PID */
   ti->pid = pid;

   ti->pi = pi;
   bintree_node_init(&ti->tree_by_tid);
   list_node_init(&ti->runnable_list);
   list_node_init(&ti->sleeping_list);
   list_node_init(&ti->zombie_list);
   list_node_init(&ti->siblings_list);

   list_node_init(&pi->children_list);
   list_add_tail(&parent->pi->children_list, &ti->siblings_list);

   list_node_init(&pi->mappings);

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
   ti->tid = -1;
   ti->tid = thread_ti_to_tid(ti);
   ti->pid = proc->tid;
   ASSERT(thread_tid_to_ti(ti->tid) == ti);

   /* NOTE: siblings_list is used ONLY for the main task (tid == pid) */
   list_node_init(&ti->siblings_list);

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

   if (ti->tid == ti->pid) {

      ASSERT(ti->pi->ref_count > 0);

      if (ti->pi->mmap_heap) {
         kmalloc_destroy_heap(ti->pi->mmap_heap);
         kfree2(ti->pi->mmap_heap, kmalloc_get_heap_struct_size());
         ti->pi->mmap_heap = NULL;
      }

      if (--ti->pi->ref_count == 0) {
         list_remove(&ti->siblings_list);
         kfree2(ti, sizeof(task_info) + sizeof(process_info));
      }

   } else {
      kfree2(ti, sizeof(task_info));
   }
}

user_mapping *
process_add_user_mapping(fs_handle h, void *vaddr, size_t page_count)
{
   ASSERT(!is_preemption_enabled());
   ASSERT(!process_get_user_mapping(vaddr));

   process_info *pi = get_curr_task()->pi;
   user_mapping *um = kzmalloc(sizeof(user_mapping));

   if (!um)
      return NULL;

   list_node_init(&um->list);

   um->h = h;
   um->vaddr = vaddr;
   um->page_count = page_count;

   list_add_tail(&pi->mappings, &um->list);
   return um;
}

void process_remove_user_mapping(user_mapping *um)
{
   ASSERT(!is_preemption_enabled());

   list_remove(&um->list);
   kfree2(um, sizeof(user_mapping));
}

user_mapping *process_get_user_mapping(void *vaddr)
{
   ASSERT(!is_preemption_enabled());

   process_info *pi = get_curr_task()->pi;
   user_mapping *pos, *temp;

   list_for_each(pos, temp, &pi->mappings, list) {
      if (pos->vaddr == vaddr)
         return pos;
   }

   return NULL;
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
   return get_curr_task()->pid;
}

sptr sys_gettid()
{
   return get_curr_task()->tid;
}

void join_kernel_thread(int tid)
{
   task_info *ti;
   ASSERT(is_preemption_enabled());

   while ((ti = get_task(tid))) {
      wait_obj_set(&get_curr_task()->wobj, WOBJ_TASK, ti);
      task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
      kernel_yield();
   }
}

sptr sys_waitpid(int pid, int *user_wstatus, int options)
{
   ASSERT(are_interrupts_enabled());
   DEBUG_VALIDATE_STACK_PTR();

   task_info *curr = get_curr_task();

   /*
    * TODO: make waitpid() able to wait on other child state changes, in
    * particular in case a children received a SIGSTOP or a SIGCONT.
    */

   if (pid > 0) {

      /* Wait for a specific PID */

      volatile task_info *waited_task = (volatile task_info *)get_task(pid);

      if (!waited_task || waited_task->pi->parent_pid != curr->pid)
         return -ECHILD;

      while (waited_task->state != TASK_STATE_ZOMBIE) {

         wait_obj_set(&get_curr_task()->wobj,
                      WOBJ_TASK,
                      (task_info *)waited_task);
         task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
         kernel_yield();
      }

      if (user_wstatus) {
         int value = EXITCODE(waited_task->exit_status, 0);

         if (copy_to_user(user_wstatus, &value, sizeof(int)) < 0) {
            remove_task((task_info *)waited_task);
            return -EFAULT;
         }
      }

      remove_task((task_info *)waited_task);
      return pid;
   }

   /*
    * Since Tilck does not support UIDs and GIDs != 0, the values of
    *    pid < -1
    *    pid == -1
    *    pid == 0
    * are treated in the same way.
    */

   task_info *zombie_child = NULL;
   task_info *pos, *temp;

   while (true) {

      disable_preemption();

      list_for_each(pos, temp, &curr->pi->children_list, siblings_list) {
         if (pos->state == TASK_STATE_ZOMBIE) {
            zombie_child = pos;
            break;
         }
      }

      enable_preemption();

      if (zombie_child)
         break;

      /* No zombie child has been found */

      if (options & WNOHANG) {
         /* With WNOHANG we must not hang until a child dies */
         return 0;
      }

      /* Hang until a child dies */
      wait_obj_set(&curr->wobj, WOBJ_TASK, (task_info *)-1);
      task_change_state(curr, TASK_STATE_SLEEPING);
      kernel_yield();
   }

   if (user_wstatus) {

      int value = EXITCODE(zombie_child->exit_status, 0);

      if (copy_to_user(user_wstatus, &value, sizeof(int)) < 0) {
         remove_task(zombie_child);
         return -EFAULT;
      }
   }

   int zombie_child_pid = zombie_child->tid;
   remove_task(zombie_child);
   return zombie_child_pid;
}

sptr sys_wait4(int pid, int *user_wstatus, int options, void *user_rusage)
{
   char zero_buf[136] = {0};

   if (user_rusage) {
      // TODO: update when rusage is actually supported
      if (copy_to_user(user_rusage, zero_buf, sizeof(zero_buf)) < 0)
         return -EFAULT;
   }

   return sys_waitpid(pid, user_wstatus, options);
}

NORETURN sptr sys_exit(int exit_status)
{
   disable_preemption();
   task_info *curr = get_curr_task();
   int cppid = curr->pi->parent_pid;

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

   // Remove all the user mappings

   while (!list_is_empty(&curr->pi->mappings)) {

      user_mapping *um =
         list_first_obj(&curr->pi->mappings, user_mapping, list);

      size_t actual_len = um->page_count << PAGE_SHIFT;

      fs_handle_base *hb = um->h;
      hb->fops.munmap(hb, um->vaddr, actual_len);

      per_heap_kfree(curr->pi->mmap_heap,
                     um->vaddr,
                     &actual_len,
                     KFREE_FL_ALLOW_SPLIT |
                     KFREE_FL_MULTI_STEP  |
                     KFREE_FL_NO_ACTUAL_FREE);

      process_remove_user_mapping(um);
   }

   /*
    * TODO: iterate over all the children of this process and make their
    * parent to be the parent of this process.
    */

   // Wake-up all the tasks waiting on this task to exit

   DEBUG_ONLY(debug_check_tasks_lists());

   task_info *pos, *temp;

   list_for_each(pos, temp, &sleeping_tasks_list, sleeping_list) {

      ASSERT(pos->state == TASK_STATE_SLEEPING);

      void *woptr = pos->wobj.ptr;

      if (woptr == curr || (pos->pid == cppid && woptr == (void *)-1)) {

         uptr var;
         disable_interrupts(&var);
         {
            if (pos->wobj.ptr == woptr) {
               ASSERT(pos->wobj.type == WOBJ_TASK);
               wait_obj_reset(&pos->wobj);
               task_change_state(pos, TASK_STATE_RUNNABLE);
            }
         }
         enable_interrupts(&var);
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

sptr sys_getppid()
{
   return get_curr_task()->pi->parent_pid;
}

sptr sys_prctl(int option, uptr a2, uptr a3, uptr a4, uptr a5)
{
   // TODO: actually implement sys_prctl()

   if (option == PR_SET_NAME) {
      // printk("[TID: %d] PR_SET_NAME '%s'\n", get_curr_task()->tid, a2);
      // TODO: save the task name in task_info.
      return 0;
   }

   printk("[TID: %d] Unknown option: %d\n", option);
   return -EINVAL;
}
