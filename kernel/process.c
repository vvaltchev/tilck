/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>
#include <tilck_gen_headers/mod_debugpanel.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/fs/vfs.h>

#include <sys/prctl.h>        // system header

STATIC_ASSERT(IS_PAGE_ALIGNED(KERNEL_STACK_SIZE));
STATIC_ASSERT(IS_PAGE_ALIGNED(IO_COPYBUF_SIZE));
STATIC_ASSERT(IS_PAGE_ALIGNED(ARGS_COPYBUF_SIZE));

#define ISOLATED_STACK_HI_VMEM_SPACE   (KERNEL_STACK_SIZE + (2 * PAGE_SIZE))

static void *alloc_kernel_isolated_stack(struct process *pi)
{
   void *vaddr_in_block;
   void *block_vaddr;
   void *direct_va;
   ulong direct_pa;
   size_t count;

   ASSERT(pi->pdir != NULL);

   direct_va = kzmalloc(KERNEL_STACK_SIZE);

   if (!direct_va)
      return NULL;

   direct_pa = LIN_VA_TO_PA(direct_va);
   block_vaddr = hi_vmem_reserve(ISOLATED_STACK_HI_VMEM_SPACE);

   if (!block_vaddr) {
      kfree2(direct_va, KERNEL_STACK_SIZE);
      return NULL;
   }

   vaddr_in_block = (void *)((ulong)block_vaddr + PAGE_SIZE);

   count = map_pages(get_kernel_pdir(),
                     vaddr_in_block,
                     direct_pa,
                     KERNEL_STACK_PAGES,
                     PAGING_FL_RW);

   if (count != KERNEL_STACK_PAGES) {
      unmap_pages(get_kernel_pdir(), vaddr_in_block, count, false);
      hi_vmem_release(block_vaddr, ISOLATED_STACK_HI_VMEM_SPACE);
      kfree2(direct_va, KERNEL_STACK_SIZE);
      return NULL;
   }

   return vaddr_in_block;
}

static void
free_kernel_isolated_stack(struct process *pi, void *vaddr_in_block)
{
   void *block_vaddr = (void *)((ulong)vaddr_in_block - PAGE_SIZE);
   ulong direct_pa = get_mapping(get_kernel_pdir(), vaddr_in_block);
   void *direct_va = PA_TO_LIN_VA(direct_pa);

   unmap_pages(get_kernel_pdir(), vaddr_in_block, KERNEL_STACK_PAGES, false);
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

static void alloc_kernel_stack(struct task *ti)
{
   if (KERNEL_STACK_ISOLATION) {
      ti->kernel_stack = alloc_kernel_isolated_stack(ti->pi);
   } else {
      ti->kernel_stack = kzmalloc(KERNEL_STACK_SIZE);
   }
}

static void free_kernel_stack(struct task *ti)
{
   if (KERNEL_STACK_ISOLATION) {
      free_kernel_isolated_stack(ti->pi, ti->kernel_stack);
   } else {
      kfree2(ti->kernel_stack, KERNEL_STACK_SIZE);
   }
}

static bool do_common_task_allocs(struct task *ti, bool alloc_bufs)
{
   alloc_kernel_stack(ti);

   if (!ti->kernel_stack)
      return false;

   if (alloc_bufs) {

      ti->io_copybuf = kmalloc(IO_COPYBUF_SIZE + ARGS_COPYBUF_SIZE);

      if (!ti->io_copybuf) {
         free_kernel_stack(ti);
         return false;
      }

      ti->args_copybuf = (void *)((ulong)ti->io_copybuf + IO_COPYBUF_SIZE);
   }
   return true;
}

void process_free_mappings_info(struct process *pi)
{
   struct mappings_info *mi = pi->mi;

   if (mi && !pi->vforked) {
      ASSERT(mi->mmap_heap);
      kmalloc_destroy_heap(mi->mmap_heap);
      kfree2(mi->mmap_heap, kmalloc_get_heap_struct_size());
      kfree_obj(mi, struct mappings_info);
      pi->mi = NULL;
   }
}

void free_common_task_allocs(struct task *ti)
{
   struct process *pi = ti->pi;
   process_free_mappings_info(pi);

   free_kernel_stack(ti);
   kfree2(ti->io_copybuf, IO_COPYBUF_SIZE + ARGS_COPYBUF_SIZE);

   ti->io_copybuf = NULL;
   ti->args_copybuf = NULL;
   ti->kernel_stack = NULL;
}

void free_mem_for_zombie_task(struct task *ti)
{
   ASSERT_TASK_STATE(ti->state, TASK_STATE_ZOMBIE);

#if DEBUG_CHECKS
   if (ti == get_curr_task()) {
      volatile ulong stack_var = 123;
      if (!IN_RANGE((ulong)&stack_var & PAGE_MASK, init_st_begin, init_st_end))
         panic("free_mem_for_zombie_task() called w/o switch to initial stack");
   }
#endif

   free_common_task_allocs(ti);

   if (ti->pi->automatic_reaping) {
      /* The SIGCHLD signal has been EXPLICITLY ignored by the parent */
      remove_task(ti);
   }
}

void init_task_lists(struct task *ti)
{
   bintree_node_init(&ti->tree_by_tid_node);
   list_node_init(&ti->runnable_node);
   list_node_init(&ti->wakeup_timer_node);
   list_node_init(&ti->siblings_node);

   list_init(&ti->tasks_waiting_list);
   list_init(&ti->on_exit);
   bzero(&ti->wobj, sizeof(struct wait_obj));
}

void init_process_lists(struct process *pi)
{
   list_init(&pi->children);
   kmutex_init(&pi->fslock, KMUTEX_FL_RECURSIVE);
}

struct task *
allocate_new_process(struct task *parent, int pid, pdir_t *new_pdir)
{
   struct process *pi, *parent_pi = parent->pi;
   struct task *ti = NULL;
   bool common_allocs = false;
   bool arch_fields = false;

   if (UNLIKELY(!(ti = kmalloc(TOT_PROC_AND_TASK_SIZE))))
      goto oom_case;

   pi = (struct process *)(ti + 1);

   /* The first process (init) has as parent == kernel_process */
   ASSERT(parent != NULL);

   memcpy(ti, parent, sizeof(struct task));
   memcpy(pi, parent_pi, sizeof(struct process));

   if (MOD_debugpanel) {

      if (UNLIKELY(!(pi->debug_cmdline = kzmalloc(PROCESS_CMDLINE_BUF_SIZE))))
         goto oom_case;

      if (parent_pi->debug_cmdline) {
         memcpy(pi->debug_cmdline,
                parent_pi->debug_cmdline,
                PROCESS_CMDLINE_BUF_SIZE);
      }
   }

   pi->parent_pid = parent_pi->pid;
   pi->pdir = new_pdir;
   pi->ref_count = 1;
   pi->pid = pid;
   pi->did_call_execve = false;
   pi->automatic_reaping = false;
   pi->cwd.fs = NULL;
   pi->vforked = false;

   if (new_pdir != parent_pi->pdir) {

      if (parent_pi->mi) {
         if (UNLIKELY(!(pi->mi = duplicate_mappings_info(pi, parent_pi->mi))))
            goto oom_case;
      }

      if (pi->elf)
         retain_subsys_flock(pi->elf);

   } else {
      pi->vforked = true;
   }

   pi->inherited_mmap_heap = !!pi->mi;
   ti->pi = pi;
   ti->tid = pid;
   ti->is_main_thread = true;
   ti->timer_ready = false;

   /*
    * From fork(2):
    *    The child's set of pending signals is initially empty.
    *
    * From sigpending(2):
    *    A child created via fork(2) initially has an empty pending signal
    *    set; the pending signal set is preserved across an execve(2).
    */
   drop_all_pending_signals(ti);

   /* Reset sched ticks in the new process */
   bzero(&ti->ticks, sizeof(ti->ticks));

   /* Copy parent's `cwd` while retaining the `fs` and the inode obj */
   process_set_cwd2_nolock_raw(pi, &parent_pi->cwd);

   if (UNLIKELY(!(common_allocs = do_common_task_allocs(ti, true))))
      goto oom_case;

   if (UNLIKELY(!(arch_fields = arch_specific_new_task_setup(ti, parent))))
      goto oom_case;

   arch_specific_new_proc_setup(pi, parent_pi); // NOTE: cannot fail
   init_task_lists(ti);
   init_process_lists(pi);
   list_add_tail(&parent_pi->children, &ti->siblings_node);

   pi->proc_tty = parent_pi->proc_tty;
   return ti;

oom_case:

   if (ti) {

      if (arch_fields)
         arch_specific_free_task(ti);

      if (common_allocs)
         free_common_task_allocs(ti);

      if (pi->cwd.fs) {
         vfs_release_inode_at(&pi->cwd);
         release_obj(pi->cwd.fs);
      }

      process_free_mappings_info(ti->pi);

      if (MOD_debugpanel && pi->debug_cmdline)
         kfree2(pi->debug_cmdline, PROCESS_CMDLINE_BUF_SIZE);

      kfree2(ti, TOT_PROC_AND_TASK_SIZE);
   }

   return NULL;
}

struct task *allocate_new_thread(struct process *pi, int tid, bool alloc_bufs)
{
   ASSERT(pi != NULL);
   struct task *process_task = get_process_task(pi);
   struct task *ti = kzalloc_obj(struct task);

   if (!ti || !(ti->pi = pi) || !do_common_task_allocs(ti, alloc_bufs)) {

      if (ti) /* do_common_task_allocs() failed */
         free_common_task_allocs(ti);

      kfree_obj(ti, struct task);
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

   if (release_obj(pi) == 0) {

      arch_specific_free_proc(pi);
      kfree2(get_process_task(pi), TOT_PROC_AND_TASK_SIZE);

      if (MOD_debugpanel)
         kfree2(pi->debug_cmdline, PROCESS_CMDLINE_BUF_SIZE);
   }
}

void free_task(struct task *ti)
{
   ASSERT_TASK_STATE(ti->state, TASK_STATE_ZOMBIE);
   arch_specific_free_task(ti);

   ASSERT(!ti->kernel_stack);
   ASSERT(!ti->io_copybuf);
   ASSERT(!ti->args_copybuf);

   list_remove(&ti->siblings_node);

   if (is_main_thread(ti))
      free_process_int(ti->pi);
   else
      kfree_obj(ti, struct task);
}

void *task_temp_kernel_alloc(size_t size)
{
   struct task *curr = get_curr_task();
   void *ptr = NULL;

   disable_preemption();
   {
      ptr = kmalloc(size);

      if (ptr) {

         struct kernel_alloc *alloc = kzalloc_obj(struct kernel_alloc);

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
      alloc = bintree_find_ptr(curr->kallocs_tree_root,
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

      kfree_obj(alloc, struct kernel_alloc);
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
   struct process *pi = get_curr_proc();
   mode_t old = pi->umask;
   pi->umask = mask & 0777;
   return old;
}

int sys_getpid(void)
{
   return get_curr_proc()->pid;
}

int sys_gettid(void)
{
   return get_curr_task()->tid;
}

int kthread_join(int tid, bool ignore_signals)
{
   struct task *curr = get_curr_task();
   struct task *ti;
   int rc = 0;

   ASSERT(is_preemption_enabled());
   disable_preemption();

   while ((ti = get_task(tid))) {

      if (pending_signals()) {

         wait_obj_reset(&curr->wobj);

         if (!ignore_signals) {
            rc = -EINTR;
            break;
         }
      }

      prepare_to_wait_on(WOBJ_TASK,
                         TO_PTR(ti->tid),
                         NO_EXTRA,
                         &ti->tasks_waiting_list);

      enter_sleep_wait_state();
      /* after enter_sleep_wait_state() the preemption is be enabled */

      disable_preemption();
   }

   enable_preemption();
   return rc;
}

int kthread_join_all(const int *tids, size_t n, bool ignore_signals)
{
   int rc = 0;

   for (size_t i = 0; i < n; i++) {

      rc = kthread_join(tids[i], ignore_signals);

      if (rc)
         break;
   }

   return rc;
}

void
handle_vforked_child_move_on(struct process *pi)
{
   struct task *parent;

   ASSERT(!is_preemption_enabled());
   ASSERT(pi->vforked);
   parent = get_task(pi->parent_pid);

   ASSERT(parent != NULL);
   ASSERT(parent->stopped);
   ASSERT(parent->vfork_stopped);

   parent->stopped = false;
   parent->vfork_stopped = false;

   pi->vforked = false;
}

int sys_getppid(void)
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

int sys_prctl(int option, ulong a2, ulong a3, ulong a4, ulong a5)
{
   // TODO: actually implement sys_prctl()

   if (option == PR_SET_NAME) {
      // printk("[TID: %d] PR_SET_NAME '%s'\n", get_curr_task()->tid, a2);
      // TODO: save the task name in struct task.
      return 0;
   }

   printk("[TID: %d] Unknown option: %d\n", get_curr_tid(), option);
   return -EINVAL;
}
