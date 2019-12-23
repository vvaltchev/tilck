/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_modules.h>
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
#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/sys_types.h>

#include <sys/prctl.h>        // system header

#define ISOLATED_STACK_HI_VMEM_SPACE   (KERNEL_STACK_SIZE + (2 * PAGE_SIZE))

static void *alloc_kernel_isolated_stack(struct process *pi)
{
   void *vaddr_in_block;
   void *block_vaddr;
   void *direct_va;
   uptr direct_pa;
   size_t count;

   ASSERT(pi->pdir != NULL);

   direct_va = kzmalloc(KERNEL_STACK_SIZE);

   if (!direct_va)
      return NULL;

   direct_pa = KERNEL_VA_TO_PA(direct_va);
   block_vaddr = hi_vmem_reserve(ISOLATED_STACK_HI_VMEM_SPACE);

   if (!block_vaddr) {
      kfree2(direct_va, KERNEL_STACK_SIZE);
      return NULL;
   }

   vaddr_in_block = (void *)((uptr)block_vaddr + KERNEL_STACK_SIZE);

   count = map_pages(pi->pdir,
                     vaddr_in_block,
                     direct_pa,
                     KERNEL_STACK_SIZE / PAGE_SIZE,
                     false,   /* big pages allowed */
                     false,   /* user accessible */
                     true);   /* writable */

   if (count != KERNEL_STACK_SIZE / PAGE_SIZE) {
      unmap_pages(pi->pdir, vaddr_in_block, count, false);
      hi_vmem_release(block_vaddr, ISOLATED_STACK_HI_VMEM_SPACE);
      kfree2(direct_va, KERNEL_STACK_SIZE);
      return NULL;
   }

   return vaddr_in_block;
}

static void
free_kernel_isolated_stack(struct process *pi, void *vaddr_in_block)
{
   void *block_vaddr = (void *)((uptr)vaddr_in_block - KERNEL_STACK_SIZE);
   uptr direct_pa = get_mapping(pi->pdir, vaddr_in_block);
   void *direct_va = KERNEL_PA_TO_VA(direct_pa);

   unmap_pages(pi->pdir, vaddr_in_block, KERNEL_STACK_SIZE / PAGE_SIZE, false);
   hi_vmem_release(block_vaddr, ISOLATED_STACK_HI_VMEM_SPACE);
   kfree2(direct_va, KERNEL_STACK_SIZE);
}


#define TOT_IOBUF_AND_ARGS_BUF_PG (USER_ARGS_PAGE_COUNT + USER_ARGS_PAGE_COUNT)

STATIC_ASSERT(
   TOT_IOBUF_AND_ARGS_BUF_PG ==  2     ||
   TOT_IOBUF_AND_ARGS_BUF_PG ==  4     ||
   TOT_IOBUF_AND_ARGS_BUF_PG ==  8     ||
   TOT_IOBUF_AND_ARGS_BUF_PG == 16     ||
   TOT_IOBUF_AND_ARGS_BUF_PG == 32     ||
   TOT_IOBUF_AND_ARGS_BUF_PG == 64
);

#undef TOT_IOBUF_AND_ARGS_BUF_PG

static bool do_common_task_allocations(struct task *ti, bool alloc_bufs)
{
   if (KERNEL_STACK_ISOLATION) {
      ti->kernel_stack = alloc_kernel_isolated_stack(ti->pi);
   } else {
      ti->kernel_stack = kzmalloc(KERNEL_STACK_SIZE);
   }

   if (!ti->kernel_stack)
      return false;

   if (alloc_bufs) {
      ti->io_copybuf = kmalloc(IO_COPYBUF_SIZE + ARGS_COPYBUF_SIZE);

      if (!ti->io_copybuf) {
         kfree2(ti->kernel_stack, KERNEL_STACK_SIZE);
         return false;
      }

      ti->args_copybuf = (void *)((uptr)ti->io_copybuf + IO_COPYBUF_SIZE);
   }
   return true;
}

static void internal_free_mem_for_zombie_task(struct task *ti)
{
   if (KERNEL_STACK_ISOLATION) {
      free_kernel_isolated_stack(ti->pi, ti->kernel_stack);
   } else {
      kfree2(ti->kernel_stack, KERNEL_STACK_SIZE);
   }

   kfree2(ti->io_copybuf, IO_COPYBUF_SIZE + ARGS_COPYBUF_SIZE);

   ti->io_copybuf = NULL;
   ti->args_copybuf = NULL;
   ti->kernel_stack = NULL;
}

void free_mem_for_zombie_task(struct task *ti)
{
   ASSERT(ti->state == TASK_STATE_ZOMBIE);

#ifdef DEBUG

   if (ti == get_curr_task()) {

      uptr stack_var = 123;
      if (!IN_RANGE((uptr)&stack_var & PAGE_MASK, init_st_begin, init_st_end))
         panic("free_mem_for_zombie_task() called w/o switch to initial stack");
   }

#endif

   internal_free_mem_for_zombie_task(ti);
}

void init_task_lists(struct task *ti)
{
   bintree_node_init(&ti->tree_by_tid_node);
   list_node_init(&ti->runnable_node);
   list_node_init(&ti->sleeping_node);
   list_node_init(&ti->zombie_node);
   list_node_init(&ti->wakeup_timer_node);
   list_node_init(&ti->siblings_node);

   list_init(&ti->tasks_waiting_list);
   bzero(&ti->wobj, sizeof(struct wait_obj));
}

void init_process_lists(struct process *pi)
{
   list_init(&pi->children);
   list_init(&pi->mappings);

   kmutex_init(&pi->fslock, KMUTEX_FL_RECURSIVE);
}

struct task *
allocate_new_process(struct task *parent, int pid, pdir_t *new_pdir)
{
   struct process *pi, *parent_pi = parent->pi;
   struct task *ti = kmalloc(
      sizeof(struct task) + sizeof(struct process)
   );

   if (!ti)
      return NULL;

   pi = (struct process *)(ti + 1);

   /* The first process (init) has as parent == kernel_process */
   ASSERT(parent != NULL);

   memcpy(ti, parent, sizeof(struct task));
   memcpy(pi, parent_pi, sizeof(struct process));

   if (MOD_debugpanel) {

      if (!(pi->debug_cmdline = kzmalloc(PROCESS_CMDLINE_BUF_SIZE))) {
         kfree2(ti, sizeof(struct task) + sizeof(struct process));
         return NULL;
      }

      if (parent_pi->debug_cmdline) {
         memcpy(pi->debug_cmdline,
                parent_pi->debug_cmdline,
                PROCESS_CMDLINE_BUF_SIZE);
      }
   }

   pi->parent_pid = parent_pi->pid;
   pi->mmap_heap = kmalloc_heap_dup(parent_pi->mmap_heap);

   pi->pdir = new_pdir;
   pi->ref_count = 1;
   pi->pid = pid;
   ti->tid = pid;
   ti->is_main_thread = true;
   pi->did_call_execve = false;
   ti->pi = pi;

   /* Copy parent's `cwd` while retaining the `fs` and the inode obj */
   process_set_cwd2_nolock_raw(pi, &parent_pi->cwd);

   if (!do_common_task_allocations(ti, true) ||
       !arch_specific_new_task_setup(ti, parent))
   {
      if (pi->mmap_heap) {
         kmalloc_destroy_heap(pi->mmap_heap);
         kfree2(pi->mmap_heap, kmalloc_get_heap_struct_size());
      }

      kfree2(ti, sizeof(struct task) + sizeof(struct process));
      return NULL;
   }

   init_task_lists(ti);
   init_process_lists(pi);
   list_add_tail(&parent_pi->children, &ti->siblings_node);

   pi->proc_tty = parent_pi->proc_tty;
   return ti;
}

struct task *allocate_new_thread(struct process *pi, int tid, bool alloc_bufs)
{
   struct task *process_task = get_process_task(pi);
   struct task *ti = kzmalloc(sizeof(struct task));

   if (!ti || !(ti->pi=pi) || !do_common_task_allocations(ti, alloc_bufs)) {
      kfree2(ti, sizeof(struct task));
      return NULL;
   }

   ti->tid = tid;
   ti->is_main_thread = false;

   init_task_lists(ti);
   arch_specific_new_task_setup(ti, process_task);
   return ti;
}

static void free_process_int(struct process *pi)
{
   ASSERT(get_ref_count(pi) > 0);

   if (pi->mmap_heap) {
      kmalloc_destroy_heap(pi->mmap_heap);
      kfree2(pi->mmap_heap, kmalloc_get_heap_struct_size());
      pi->mmap_heap = NULL;
   }

   if (release_obj(pi) == 0) {

      kfree2(get_process_task(pi),
             sizeof(struct task) + sizeof(struct process));

      if (MOD_debugpanel)
         kfree2(pi->debug_cmdline, PROCESS_CMDLINE_BUF_SIZE);
   }

   if (LIKELY(pi->cwd.fs != NULL)) {

      /*
       * When we change the current directory or when we fork a process, we
       * set a new value for the struct vfs_path pi->cwd which has its inode
       * retained as well as its owning fs. Here we have to release those
       * ref-counts.
       */

      vfs_release_inode_at(&pi->cwd);
      release_obj(pi->cwd.fs);
   }
}

void free_task(struct task *ti)
{
   ASSERT(ti->state == TASK_STATE_ZOMBIE);
   arch_specific_free_task(ti);

   ASSERT(!ti->kernel_stack);
   ASSERT(!ti->io_copybuf);
   ASSERT(!ti->args_copybuf);

   list_remove(&ti->siblings_node);

   if (is_main_thread(ti))
      free_process_int(ti->pi);
   else
      kfree2(ti, sizeof(struct task));
}

void *task_temp_kernel_alloc(size_t size)
{
   struct task *curr = get_curr_task();
   void *ptr = NULL;

   disable_preemption();
   {
      ptr = kmalloc(size);

      if (ptr) {

         struct kernel_alloc *alloc = kzmalloc(sizeof(struct kernel_alloc));

         if (alloc) {

            bintree_node_init(&alloc->node);
            alloc->vaddr = ptr;
            alloc->size = size;

            bintree_insert_ptr(&curr->kallocs_tree_root,
                               alloc,
                               struct kernel_alloc,
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
   struct task *curr = get_curr_task();
   struct kernel_alloc *alloc;

   if (!ptr)
      return;

   disable_preemption();
   {
      alloc = bintree_find_ptr(&curr->kallocs_tree_root,
                               ptr,
                               struct kernel_alloc,
                               node,
                               vaddr);

      ASSERT(alloc != NULL);

      kfree2(alloc->vaddr, alloc->size);

      bintree_remove_ptr(&curr->kallocs_tree_root,
                         alloc,
                         struct kernel_alloc,
                         node,
                         vaddr);

      kfree2(alloc, sizeof(struct kernel_alloc));
   }
   enable_preemption();
}

void set_kernel_process_pdir(pdir_t *pdir)
{
   kernel_process_pi->pdir = pdir;
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
   struct process *pi = get_curr_task()->pi;
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
   struct task *ti;

   ASSERT(is_preemption_enabled());
   disable_preemption();

   while ((ti = get_task(tid))) {

      task_set_wait_obj(get_curr_task(),
                        WOBJ_TASK,
                        TO_PTR(ti->tid),
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

static void close_all_handles(struct process *pi)
{
   for (u32 i = 0; i < MAX_HANDLES; i++) {

      fs_handle *h = pi->handles[i];

      if (h) {
         vfs_close2(pi, h);
         pi->handles[i] = NULL;
      }
   }
}

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

         wake_up_tasks_waiting_on(pos);
      }
   }
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

   struct process *pi = ti->pi;

   if (ti->state == TASK_STATE_ZOMBIE)
      return; /* do nothing, the task is already dead */

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
   remove_all_user_zero_mem_mappings(pi);
   ASSERT(list_is_empty(&pi->mappings));

   if (ti->tid != 1) {

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
   wake_up_tasks_waiting_on(ti);

   if (term_sig) {

      /*
       * The process has been killed. It makes sense to perform some additional
       * actions, in order to preserve the system in a healty state.
       */

      if (pi->did_set_tty_medium_raw)
         tty_set_medium_raw_mode(pi->proc_tty, false);
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

static int fork_dup_all_handles(struct process *pi)
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
   struct task *child = NULL;
   struct task *curr = get_curr_task();
   struct process *curr_pi = curr->pi;
   pdir_t *new_pdir = NULL;

   disable_preemption();

   if ((pid = create_new_pid()) < 0)
      goto out; /* NOTE: is already set to -EAGAIN */

   if (FORK_NO_COW)
      new_pdir = pdir_deep_clone(curr_pi->pdir);
   else
      new_pdir = pdir_clone(curr_pi->pdir);

   if (!new_pdir)
      goto no_mem_exit;

   if (!(child = allocate_new_process(curr, pid, new_pdir)))
      goto no_mem_exit;

   /* Set new_pdir in order to avoid its double-destruction */
   new_pdir = NULL;

   /* Child's kernel stack must be set */
   ASSERT(child->kernel_stack != NULL);

   if (child->state == TASK_STATE_RUNNING)
      child->state = TASK_STATE_RUNNABLE;

   child->running_in_kernel = false;
   task_info_reset_kernel_stack(child);

   child->state_regs--; // make room for a regs_t struct in child's stack
   *child->state_regs = *curr->state_regs; // copy parent's regs_t
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

   set_curr_pdir(curr_pi->pdir);
   enable_preemption();
   return child->tid;


no_mem_exit:

   rc = -ENOMEM;

   if (new_pdir)
      pdir_destroy(new_pdir);

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
   return get_curr_proc()->parent_pid;
}

/* create new session */
int sys_setsid(void)
{
   struct process *pi = get_curr_proc();
   int rc = -EPERM;

   disable_preemption();

   if (!sched_count_proc_in_group(pi->pid)) {
      pi->pgid = pi->pid;
      pi->sid = pi->pid;
      pi->proc_tty = NULL;
      rc = pi->sid;
   }

   enable_preemption();
   return rc;
}

/* get current session id */
int sys_getsid(int pid)
{
   return get_curr_proc()->sid;
}

int sys_setpgid(int pid, int pgid)
{
   struct process *pi;
   int sid;
   int rc = 0;

   if (pgid < 0)
      return -EINVAL;

   disable_preemption();

   if (!pid) {

      pi = get_curr_proc();

   } else {

      pi = get_process(pid);

      if (!pi) {
         rc = -ESRCH;
         goto out;
      }

      if (pi->did_call_execve) {
         rc = -EACCES;
         goto out;
      }

      /* Cannot move processes in other sessions */
      if (pi->sid != get_curr_proc()->sid) {
         rc = -EPERM;
         goto out;
      }
   }

   if (pgid) {

      sid = sched_get_session_of_group(pgid);

      /*
      * If the process group exists (= there's at least one process with
      * pi->pgid == pgid), it must be in the same session as `pid` and the
      * calling process.
      */
      if (sid >= 0 && sid != pi->sid) {
         rc = -EPERM;
         goto out;
      }

      /* Set process' pgid to `pgid` */
      pi->pgid = pgid;

   } else {

      /* pgid is 0: make the process a group leader */
      pi->pgid = pi->pid;
   }

out:
   enable_preemption();
   return rc;
}

int sys_getpgid(int pid)
{
   struct process *pi;
   int ret = -ESRCH;

   if (!pid)
      return get_curr_proc()->pgid;

   disable_preemption();
   {
      if ((pi = get_process(pid)))
         ret = pi->pgid;
   }
   enable_preemption();
   return ret;
}

int sys_getpgrp(void)
{
   return get_curr_proc()->pgid;
}

int sys_prctl(int option, uptr a2, uptr a3, uptr a4, uptr a5)
{
   // TODO: actually implement sys_prctl()

   if (option == PR_SET_NAME) {
      // printk("[TID: %d] PR_SET_NAME '%s'\n", get_curr_task()->tid, a2);
      // TODO: save the task name in struct task.
      return 0;
   }

   printk("[TID: %d] Unknown option: %d\n", option);
   return -EINVAL;
}
