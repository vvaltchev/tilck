/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/syscalls.h>

#include <sys/prctl.h>        // system header
#include <sys/wait.h>         // system header

//#define DEBUG_printk printk
#define DEBUG_printk(...)

#define EXITCODE(ret, sig)  ((ret) << 8 | (sig))
#define STOPCODE(sig) ((sig) << 8 | 0x7f)
#define CONTINUED 0xffff
#define COREFLAG 0x80

static bool do_common_task_allocations(task_info *ti)
{
   ti->kernel_stack = kzmalloc(KERNEL_STACK_SIZE);

   if (!ti->kernel_stack)
      return false;

   ti->io_copybuf = kmalloc(IO_COPYBUF_SIZE + ARGS_COPYBUF_SIZE);

   if (!ti->io_copybuf) {
      kfree2(ti->kernel_stack, KERNEL_STACK_SIZE);
      return false;
   }

   ti->args_copybuf = (void *)((uptr)ti->io_copybuf + IO_COPYBUF_SIZE);
   return true;
}

static void internal_free_mem_for_zombie_task(task_info *ti)
{
   kfree2(ti->io_copybuf, IO_COPYBUF_SIZE + ARGS_COPYBUF_SIZE);
   kfree2(ti->kernel_stack, KERNEL_STACK_SIZE);

   ti->io_copybuf = NULL;
   ti->args_copybuf = NULL;
   ti->kernel_stack = NULL;
}

void free_mem_for_zombie_task(task_info *ti)
{
   ASSERT(ti->state == TASK_STATE_ZOMBIE);

#ifdef DEBUG

   if (ti == get_curr_task()) {
      uptr stack_var = 123;
      if (((uptr)&stack_var & PAGE_MASK) != (uptr)&kernel_initial_stack)
         panic("free_mem_for_zombie_task() called w/o switch to initial stack");
   }

#endif

   internal_free_mem_for_zombie_task(ti);
}

void init_task_lists(task_info *ti)
{
   bintree_node_init(&ti->tree_by_tid_node);
   list_node_init(&ti->runnable_node);
   list_node_init(&ti->sleeping_node);
   list_node_init(&ti->zombie_node);
   list_node_init(&ti->wakeup_timer_node);

   list_init(&ti->tasks_waiting_list);
   bzero(&ti->wobj, sizeof(wait_obj));
}

void init_process_lists(process_info *pi)
{
   list_init(&pi->children_list);
   list_init(&pi->mappings);
   list_node_init(&pi->siblings_node);

   kmutex_init(&pi->fslock, KMUTEX_FL_RECURSIVE);
}

task_info *allocate_new_process(task_info *parent, int pid)
{
   process_info *pi;
   task_info *ti = kmalloc(sizeof(task_info) + sizeof(process_info));

   if (!ti)
      return NULL;

   pi = (process_info *)(ti + 1);

   /* The first process (init) has as parent == kernel_process */
   ASSERT(parent != NULL);

   memcpy(ti, parent, sizeof(task_info));
   memcpy(pi, parent->pi, sizeof(process_info));
   pi->parent_pid = parent->pi->pid;
   pi->mmap_heap = kmalloc_heap_dup(parent->pi->mmap_heap);

   pi->ref_count = 1;
   pi->pid = pid;
   ti->tid = pid;
   ti->is_main_thread = true;
   pi->did_call_execve = false;

   // TODO: retain fs/inode for pi->cwd2 is missing. This this!!

   if (!do_common_task_allocations(ti) ||
       !arch_specific_new_task_setup(ti, parent))
   {
      if (pi->mmap_heap) {
         kmalloc_destroy_heap(pi->mmap_heap);
         kfree2(pi->mmap_heap, kmalloc_get_heap_struct_size());
      }

      kfree2(ti, sizeof(task_info) + sizeof(process_info));
      return NULL;
   }

   ti->pi = pi;
   init_task_lists(ti);
   init_process_lists(pi);
   list_add_tail(&parent->pi->children_list, &pi->siblings_node);

   pi->proc_tty = parent->pi->proc_tty;
   return ti;
}

task_info *allocate_new_thread(process_info *pi)
{
   task_info *process_task = get_process_task(pi);
   task_info *ti = kzmalloc(sizeof(task_info));

   if (!ti || !do_common_task_allocations(ti)) {
      kfree2(ti, sizeof(task_info));
      return NULL;
   }

   ti->pi = pi;
   ti->tid = kthread_calc_tid(ti);
   ti->is_main_thread = false;
   ASSERT(kthread_get_ptr(ti->tid) == ti);

   init_task_lists(ti);
   arch_specific_new_task_setup(ti, process_task);
   return ti;
}

void free_task(task_info *ti)
{
   ASSERT(ti->state == TASK_STATE_ZOMBIE);
   arch_specific_free_task(ti);

   ASSERT(!ti->kernel_stack);
   ASSERT(!ti->io_copybuf);
   ASSERT(!ti->args_copybuf);

   if (is_main_thread(ti)) {

      ASSERT(get_ref_count(ti->pi) > 0);

      if (ti->pi->mmap_heap) {
         kmalloc_destroy_heap(ti->pi->mmap_heap);
         kfree2(ti->pi->mmap_heap, kmalloc_get_heap_struct_size());
         ti->pi->mmap_heap = NULL;
      }

      if (release_obj(ti->pi) == 0) {
         list_remove(&ti->pi->siblings_node);
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

   list_node_init(&um->node);

   um->h = h;
   um->vaddr = vaddr;
   um->page_count = page_count;

   list_add_tail(&pi->mappings, &um->node);
   return um;
}

void process_remove_user_mapping(user_mapping *um)
{
   ASSERT(!is_preemption_enabled());

   list_remove(&um->node);
   kfree2(um, sizeof(user_mapping));
}

user_mapping *process_get_user_mapping(void *vaddr)
{
   ASSERT(!is_preemption_enabled());

   process_info *pi = get_curr_task()->pi;
   user_mapping *pos;

   list_for_each_ro(pos, &pi->mappings, node) {
      if (pos->vaddr == vaddr)
         return pos;
   }

   return NULL;
}

void *task_temp_kernel_alloc(size_t size)
{
   task_info *curr = get_curr_task();
   void *ptr = NULL;

   disable_preemption();
   {
      ptr = kmalloc(size);

      if (ptr) {

         kernel_alloc *alloc = kzmalloc(sizeof(kernel_alloc));

         if (alloc) {

            bintree_node_init(&alloc->node);
            alloc->vaddr = ptr;
            alloc->size = size;

            bintree_insert_ptr(&curr->kallocs_tree_root,
                               alloc,
                               kernel_alloc,
                               node,
                               vaddr);

         } else {

            kfree2(ptr, size);
            ptr = NULL;
         }
      }
   }
   enable_preemption();
   return ptr;
}

void task_temp_kernel_free(void *ptr)
{
   task_info *curr = get_curr_task();
   kernel_alloc *alloc;

   if (!ptr)
      return;

   disable_preemption();
   {
      alloc = bintree_find_ptr(&curr->kallocs_tree_root,
                               ptr,
                               kernel_alloc,
                               node,
                               vaddr);

      ASSERT(alloc != NULL);

      kfree2(alloc->vaddr, alloc->size);

      bintree_remove_ptr(&curr->kallocs_tree_root,
                         alloc,
                         kernel_alloc,
                         node,
                         vaddr);

      kfree2(alloc, sizeof(kernel_alloc));
   }
   enable_preemption();
}

/*
 * ***************************************************************
 *
 * SYSCALLS
 *
 * ***************************************************************
 */

mode_t sys_umask(mode_t mask)
{
   process_info *pi = get_curr_task()->pi;
   mode_t old = pi->umask;
   pi->umask = mask & 0777;
   return old;
}

int sys_pause()
{
   task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
   kernel_yield();
   return 0;
}

int sys_getpid()
{
   return get_curr_task()->pi->pid;
}

int sys_gettid()
{
   return get_curr_task()->tid;
}

void kthread_join(int tid)
{
   task_info *ti;

   ASSERT(is_preemption_enabled());
   disable_preemption();

   while ((ti = get_task(tid))) {

      task_set_wait_obj(get_curr_task(),
                        WOBJ_TASK,
                        ti,
                        &ti->tasks_waiting_list);

      enable_preemption();
      kernel_yield();
      disable_preemption();
   }

   enable_preemption();
}

void kthread_join_all(const int *tids, size_t n)
{
   for (size_t i = 0; i < n; i++)
      kthread_join(tids[i]);
}

static int wait_for_single_pid(int pid, int *user_wstatus)
{
   ASSERT(!is_preemption_enabled());

   task_info *curr = get_curr_task();
   task_info *waited_task = get_task(pid);

   if (!waited_task || waited_task->pi->parent_pid != curr->pi->pid) {
      return -ECHILD;
   }

   while (waited_task->state != TASK_STATE_ZOMBIE) {

      task_set_wait_obj(curr,
                        WOBJ_TASK,
                        waited_task,
                        &waited_task->tasks_waiting_list);

      enable_preemption();
      kernel_yield();
      disable_preemption();
   }

   if (user_wstatus) {
      if (copy_to_user(user_wstatus,
                       &waited_task->exit_wstatus,
                       sizeof(s32)) < 0)
      {
         remove_task((task_info *)waited_task);
         return -EFAULT;
      }
   }

   remove_task((task_info *)waited_task);
   return pid;
}

int sys_waitpid(int pid, int *user_wstatus, int options)
{
   task_info *curr = get_curr_task();
   task_info *zombie = NULL;
   int zombie_tid = -1;

   ASSERT(are_interrupts_enabled());
   DEBUG_VALIDATE_STACK_PTR();

   /*
    * TODO: make waitpid() able to wait on other child state changes, in
    * particular in case a children received a SIGSTOP or a SIGCONT.
    */

   if (pid > 0) {

      /* Wait for a specific PID */

      disable_preemption();
      {
         pid = wait_for_single_pid(pid, user_wstatus);
      }
      enable_preemption();
      return pid;
   }

   /*
    * Since Tilck does not support process groups yet, the following cases:
    *    pid < -1
    *    pid == -1
    *    pid == 0
    *  are treated in the same way.
    *
    * TODO: update this code when process groups are supported.
    */


   while (true) {

      process_info *pos;
      u32 child_count = 0;

      disable_preemption();

      list_for_each_ro(pos, &curr->pi->children_list, siblings_node) {

         task_info *ti = get_process_task(pos);
         child_count++;

         if (ti->state == TASK_STATE_ZOMBIE) {
            zombie = ti;
            zombie_tid = ti->tid;
            break;
         }
      }

      if (zombie)
         break;

      enable_preemption();

      /* No zombie child has been found */

      if (options & WNOHANG) {
         /* With WNOHANG we must not hang until a child dies */
         return 0;
      }

      if (!child_count) {
         /* No children to wait for */
         return -ECHILD;
      }

      /* Hang until a child dies */
      task_set_wait_obj(curr, WOBJ_TASK, WOBJ_TASK_PTR_ANY_CHILD, NULL);
      kernel_yield();

   } // while (true)

   /*
    * The only way to get here is a positive branch in `if (zombie)`: this mean
    * that we have a valid `zombie` and that preemption is diabled.
    */
   ASSERT(!is_preemption_enabled());

   if (user_wstatus) {
      if (copy_to_user(user_wstatus, &zombie->exit_wstatus, sizeof(s32)) < 0) {
         zombie_tid = -EFAULT;
      }
   }

   remove_task(zombie);
   enable_preemption();
   return zombie_tid;
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

void wake_up_tasks_waiting_on(task_info *ti)
{
   wait_obj *wo_pos, *wo_temp;
   ASSERT(!is_preemption_enabled());

   list_for_each(wo_pos, wo_temp, &ti->tasks_waiting_list, wait_list_node) {

      ASSERT(wo_pos->type == WOBJ_TASK);

      task_info *task_to_wake_up = CONTAINER_OF(wo_pos, task_info, wobj);
      task_reset_wait_obj(task_to_wake_up);
   }
}

static bool task_is_waiting_on_any_child(task_info *ti)
{
   wait_obj *wobj = &ti->wobj;

   if (ti->state != TASK_STATE_SLEEPING)
      return false;

   if (wobj->type != WOBJ_TASK)
      return false;

   return wait_obj_get_ptr(wobj) == WOBJ_TASK_PTR_ANY_CHILD;
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

static void close_all_handles(process_info *pi)
{
   for (u32 i = 0; i < MAX_HANDLES; i++) {

      fs_handle *h = pi->handles[i];

      if (h) {
         vfs_close(h);
         pi->handles[i] = NULL;
      }
   }
}

static void
task_free_all_kernel_allocs(task_info *ti)
{
   ASSERT(!is_preemption_enabled());

   while (ti->kallocs_tree_root != NULL) {

      /* Save a pointer to the alloc object on the stack */
      kernel_alloc *alloc = ti->kallocs_tree_root;

      /* Free the allocated chunk */
      kfree2(alloc->vaddr, alloc->size);

      /* Remove the kernel_alloc elem from the tree */
      bintree_remove_ptr(&ti->kallocs_tree_root,
                         alloc,
                         kernel_alloc,
                         node,
                         vaddr);

      /* Free the kernel_alloc object itself */
      kfree2(alloc, sizeof(kernel_alloc));
   }
}

static void init_terminated(task_info *ti, int exit_code, int term_sig)
{
   if (DEBUG_QEMU_EXIT_ON_INIT_EXIT)
      debug_qemu_turn_off_machine();

   if (!term_sig)
      panic("Init exited with code: %d\n", exit_code);
   else
      panic("Init terminated by signal %d\n", term_sig);
}

static process_info *get_child_reaper(process_info *pi)
{
   /* TODO: support prctl(PR_SET_CHILD_SUBREAPER) */
   ASSERT(!is_preemption_enabled());

   task_info *child_reaper = get_task(1); /* init */
   VERIFY(child_reaper != NULL);

   return child_reaper->pi;
}

/*
 * NOTE: this code ASSUMES that threads do NOT exist:
 *    process = task = thread
 *
 * TODO: re-design/adapt this function when thread support is introduced
 */
void terminate_process(task_info *ti, int exit_code, int term_sig)
{
   ASSERT(!is_preemption_enabled());
   ASSERT(!is_kernel_thread(ti));

   process_info *pi = ti->pi;

   if (ti->wobj.type != WOBJ_NONE) {

      /*
       * If the task has been waiting on something, we have to reset its wobj
       * and remove its pointer from the target object's wait_list.
       */
      task_reset_wait_obj(ti);
   }

   task_change_state(ti, TASK_STATE_ZOMBIE);
   ti->exit_wstatus = EXITCODE(exit_code, term_sig);

   close_all_handles(pi);
   task_free_all_kernel_allocs(ti);
   ASSERT(list_is_empty(&pi->mappings));


   if (ti->tid != 1) {

      /*
       * What if the dying task has any children? We have to set their parent
       * to init (pid 1) or to the nearest child subreaper, once that is
       * supported.
       */

      process_info *pos, *temp;
      process_info *child_reaper = get_child_reaper(pi);

      list_for_each(pos, temp, &pi->children_list, siblings_node) {

         list_remove(&pos->siblings_node);
         list_add_tail(&child_reaper->children_list, &pos->siblings_node);
         pos->parent_pid = child_reaper->pid;
      }

   } else {

      /* The dying task is PID 1, init */
      init_terminated(ti, exit_code, term_sig);
   }

   /* Wake-up all the tasks waiting on this specific task to exit */
   wake_up_tasks_waiting_on(ti);

   if (pi->parent_pid > 0) {

      task_info *parent_task = get_task(pi->parent_pid);

      /* Wake-up the parent task if it's waiting on any child to exit */
      if (task_is_waiting_on_any_child(parent_task))
         task_reset_wait_obj(parent_task);
   }

   if (ti == get_curr_task()) {

      /* This function has been called by sys_exit(): we won't return */
      set_curr_pdir(get_kernel_pdir());
      pdir_destroy(pi->pdir);
      switch_stack_free_mem_and_schedule();
      NOT_REACHED();
   }

   pdir_destroy(pi->pdir);
   free_mem_for_zombie_task(ti);
}

static int fork_dup_all_handles(process_info *pi)
{
   for (u32 i = 0; i < MAX_HANDLES; i++) {

      int rc;
      fs_handle dup_h = NULL;
      fs_handle h = pi->handles[i];

      if (!h)
         continue;

      rc = vfs_dup(h, &dup_h);

      if (rc < 0 || !dup_h) {

         for (u32 j = 0; j < i; j++)
            vfs_close(pi->handles[j]);

         return -ENOMEM;
      }

      pi->handles[i] = dup_h;
   }

   return 0;
}

// Returns child's pid
int sys_fork(void)
{
   int pid;
   int rc = -EAGAIN;
   task_info *child = NULL;
   task_info *curr = get_curr_task();

   disable_preemption();

   if ((pid = create_new_pid()) < 0)
      goto out; /* NOTE: is already set to -EAGAIN */

   if (!(child = allocate_new_process(curr, pid)))
      goto no_mem_exit;

   ASSERT(child->kernel_stack != NULL);

   if (child->state == TASK_STATE_RUNNING)
      child->state = TASK_STATE_RUNNABLE;

   if (FORK_NO_COW)
      child->pi->pdir = pdir_deep_clone(curr->pi->pdir);
   else
      child->pi->pdir = pdir_clone(curr->pi->pdir);

   if (!child->pi->pdir)
      goto no_mem_exit;

   child->running_in_kernel = false;
   task_info_reset_kernel_stack(child);

   child->state_regs--; // make room for a regs struct in child's stack
   *child->state_regs = *curr->state_regs; // copy parent's regs
   set_return_register(child->state_regs, 0);

   // Make the parent to get child's pid as return value.
   set_return_register(curr->state_regs, (uptr) child->tid);

   if (fork_dup_all_handles(child->pi) < 0)
      goto no_mem_exit;

   add_task(child);

   /*
    * X86-specific note:
    * Force the CR3 reflush using the current (parent's) pdir.
    * Without doing that, COW on parent's pages doesn't work immediately.
    * That is better than invalidating all the pages affected, one by one.
    */

   set_curr_pdir(curr->pi->pdir);
   enable_preemption();
   return child->tid;


no_mem_exit:

   rc = -ENOMEM;

   if (child) {
      child->state = TASK_STATE_ZOMBIE;
      internal_free_mem_for_zombie_task(child);
      free_task(child);
   }

out:
   enable_preemption();
   return rc;
}

int sys_getppid()
{
   return get_curr_task()->pi->parent_pid;
}

/* create new session */
int sys_setsid(void)
{
   /*
    * This is a stub implementation of setsid(): the controlling terminal
    * of the current process is reset and the current pid is returned AS IF
    * it became the session leader process.
    *
    * TODO (future): consider actually implementing setsid()
    */

   task_info *ti = get_curr_task();
   ti->pi->proc_tty = NULL;
   return ti->pi->pid;
}

/* get current session id */
int sys_getsid(int pid)
{
   return -ENOSYS;
}

int sys_prctl(int option, uptr a2, uptr a3, uptr a4, uptr a5)
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
