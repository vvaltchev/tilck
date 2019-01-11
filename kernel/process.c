/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/elf_utils.h>

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

void init_task_lists(task_info *ti)
{
   bintree_node_init(&ti->tree_by_tid_node);
   list_node_init(&ti->runnable_node);
   list_node_init(&ti->sleeping_node);
   list_node_init(&ti->zombie_node);
   list_node_init(&ti->siblings_node); /* ONLY for the main task (tid == pid) */
   list_node_init(&ti->wakeup_timer_node);

   list_init(&ti->tasks_waiting_list);
   bzero(&ti->wobj, sizeof(wait_obj));
}

task_info *allocate_new_process(task_info *parent, int pid)
{
   process_info *pi;
   task_info *ti = kmalloc(sizeof(task_info) + sizeof(process_info));

   if (!ti)
      return NULL;

   pi = (process_info *)(ti + 1);

   /* The first process (init) has as parent 'kernel_process' */
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
   init_task_lists(ti);
   list_init(&pi->children_list);
   list_add_tail(&parent->pi->children_list, &ti->siblings_node);

   list_init(&pi->mappings);
   arch_specific_new_task_setup(ti, parent);
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

   init_task_lists(ti);
   arch_specific_new_task_setup(ti, proc);
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
         list_remove(&ti->siblings_node);
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
   user_mapping *pos, *temp;

   list_for_each(pos, temp, &pi->mappings, node) {
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

void kthread_join(int tid)
{
   task_info *ti;
   ASSERT(is_preemption_enabled());

   while ((ti = get_task(tid))) {

      task_set_wait_obj(get_curr_task(),
                        WOBJ_TASK,
                        ti,
                        &ti->tasks_waiting_list);
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

      task_info *waited_task = get_task(pid);

      if (!waited_task || waited_task->pi->parent_pid != curr->pid)
         return -ECHILD;

      while (waited_task->state != TASK_STATE_ZOMBIE) {

         task_set_wait_obj(get_curr_task(),
                           WOBJ_TASK,
                           waited_task,
                           &waited_task->tasks_waiting_list);

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

      u32 child_count = 0;
      disable_preemption();

      list_for_each(pos, temp, &curr->pi->children_list, siblings_node) {

         child_count++;

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

      if (!child_count) {
         /* No children to wait for */
         return -ECHILD;
      }

      /* Hang until a child dies */
      task_set_wait_obj(curr, WOBJ_TASK, WOBJ_TASK_PTR_ANY_CHILD, NULL);
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

void wake_up_tasks_waiting_on(task_info *ti)
{
   wait_obj *wo_pos, *wo_temp;

   list_for_each(wo_pos, wo_temp, &ti->tasks_waiting_list, wait_list_node) {

      ASSERT(wo_pos->type == WOBJ_TASK);

      task_info *ti = CONTAINER_OF(wo_pos, task_info, wobj);
      task_reset_wait_obj(ti);
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
 * NOTE: this code ASSUMES that threads does NOT exist:
 *    process = task = thread
 *
 * TODO: re-design/adapt this function when thread support is introduced
 */
void terminate_process(task_info *ti, int exit_status)
{
   ASSERT(!is_kernel_thread(ti));
   disable_preemption();

   task_change_state(ti, TASK_STATE_ZOMBIE);
   ti->exit_status = exit_status;

   // Close all of its opened handles

   for (size_t i = 0; i < ARRAY_SIZE(ti->pi->handles); i++) {

      fs_handle *h = ti->pi->handles[i];

      if (h) {
         vfs_close(h);
         ti->pi->handles[i] = NULL;
      }
   }

   // Remove all the user mappings

   while (!list_is_empty(&ti->pi->mappings)) {

      user_mapping *um =
         list_first_obj(&ti->pi->mappings, user_mapping, node);

      size_t actual_len = um->page_count << PAGE_SHIFT;

      fs_handle_base *hb = um->h;
      hb->fops.munmap(hb, um->vaddr, actual_len);

      per_heap_kfree(ti->pi->mmap_heap,
                     um->vaddr,
                     &actual_len,
                     KFREE_FL_ALLOW_SPLIT |
                     KFREE_FL_MULTI_STEP  |
                     KFREE_FL_NO_ACTUAL_FREE);

      process_remove_user_mapping(um);
   }

   /*
    * What if the current task has any children? We have to set their parent
    * to init (pid 1) or to the nearest child subreaper, once that is supported.
    *
    * TODO: support prctl(PR_SET_CHILD_SUBREAPER)
    * TODO: revisit this code once threads are supported
    */

   if (ti->tid != 1) {
      task_info *pos, *temp;
      task_info *child_reaper = get_task(1); /* init */
      ASSERT(child_reaper != NULL);

      list_for_each(pos, temp, &ti->pi->children_list, siblings_node) {

         list_remove(&pos->siblings_node);
         list_add_tail(&child_reaper->pi->children_list, &pos->siblings_node);
         pos->pi->parent_pid = child_reaper->pid;
      }
   }

   // Wake-up all the tasks waiting on this task to exit
   wake_up_tasks_waiting_on(ti);

   if (ti->pi->parent_pid > 0) {

      task_info *parent_task = get_task(ti->pi->parent_pid);

      if (task_is_waiting_on_any_child(parent_task))
         task_reset_wait_obj(parent_task);
   }

   set_page_directory(get_kernel_pdir());
   pdir_destroy(ti->pi->pdir);

#ifdef DEBUG_QEMU_EXIT_ON_INIT_EXIT
   if (ti->tid == 1) {
      debug_qemu_turn_off_machine();
   }
#endif

   if (ti == get_curr_task()) {

      /* WARNING: the following call discards the whole stack! */
      switch_to_initial_kernel_stack();

      /* Free the heap allocations used by the task, including the kernel stack */
      free_mem_for_zombie_task(get_curr_task());

      /* Run the scheduler */
      schedule_outside_interrupt_context();

   } else {
      free_mem_for_zombie_task(ti);
   }
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

static int debug_get_tn_for_tasklet_runner(task_info *ti)
{
   for (int i = 0; i < MAX_TASKLET_THREADS; i++)
      if (get_tasklet_runner(i) == ti)
         return i;

   return -1;
}

static int debug_per_task_cb(void *obj, void *arg)
{
   static const char *fmt = NO_PREFIX "| %-8d | %-3d | %-4d | %-8s | %-40s |\n";
   task_info *ti = obj;

   if (!ti->tid)
      return 0; /* skip the main kernel task */

   const char *state = debug_get_state_name(ti->state);

   if (!is_kernel_thread(ti)) {
      printk(fmt, ti->tid, ti->pid,
             ti->pi->parent_pid, state, ti->pi->filepath);
      return 0;
   }

   char buf[128];
   const char *kfunc = find_sym_at_addr((uptr)ti->what, NULL, NULL);

   if (!is_tasklet_runner(ti)) {
      snprintk(buf, sizeof(buf), "<kernel: %s>", kfunc);
   } else {
      snprintk(buf, sizeof(buf), "<kernel: %s[%d]>",
               kfunc, debug_get_tn_for_tasklet_runner(ti));
   }

   printk(fmt, ti->tid, ti->pid, ti->pi->parent_pid, state, buf);
   return 0;
}

static void debug_dump_task_table_hr(void)
{
   printk(NO_PREFIX "+----------+-----+------+----------+");
   printk(NO_PREFIX "------------------------------------------+\n");
}

void debug_show_task_list(void)
{
   printk(NO_PREFIX "\n\n");

   debug_dump_task_table_hr();

   printk(NO_PREFIX "| %-8s | %-3s | %-4s | %-8s | %-40s |\n",
          "tid", "pid", "ppid", "state", "path or kernel thread");

   debug_dump_task_table_hr();

   disable_preemption();
   {
      iterate_over_tasks(debug_per_task_cb, NULL);
   }
   enable_preemption();

   debug_dump_task_table_hr();
   printk(NO_PREFIX "\n");
}
