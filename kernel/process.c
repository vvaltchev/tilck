
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/list.h>
#include <exos/kmalloc.h>
#include <exos/process.h>
#include <exos/hal.h>
#include <exos/elf_loader.h>
#include <exos/errno.h>

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

void free_mem_for_zombie_task(task_info *ti)
{
   ASSERT(ti->state == TASK_STATE_ZOMBIE);

#ifdef DEBUG

   uptr stack_var = 123;
   if (((uptr)&stack_var & PAGE_MASK) != (uptr)&kernel_initial_stack)
      panic("free_mem_for_zombie_task() called w/o switch to initial stack");

#endif

   kfree2(ti->io_copybuf, IO_COPYBUF_SIZE + ARGS_COPYBUF_SIZE);
   kfree2(ti->kernel_stack, KTHREAD_STACK_SIZE);

   ti->io_copybuf = NULL;
   ti->args_copybuf = NULL;
   ti->kernel_stack = NULL;
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

static char *const default_env[] =
{
   "OSTYPE=linux-gnu", "EXOS=1", NULL
};

sptr sys_chdir(const char *path)
{
   sptr rc = 0;

   disable_preemption();
   {
      size_t path_len = strlen(path) + 1;

      if (path_len > ARRAY_SIZE(current->pi->cwd)) {
         rc = -ENAMETOOLONG;
         goto out;
      }

      memcpy(current->pi->cwd, path, path_len);
   }

out:
   enable_preemption();
   return rc;
}

sptr sys_getcwd(char *buf, size_t buf_size)
{
   size_t cwd_len;
   disable_preemption();
   {
      cwd_len = strlen(current->pi->cwd) + 1;

      if (!buf)
         return -EINVAL;

      if (buf_size < cwd_len)
         return -ERANGE;

      memcpy(buf, current->pi->cwd, cwd_len);
   }
   enable_preemption();
   return cwd_len;
}

sptr sys_execve(const char *filename,
                const char *const *argv,
                const char *const *env)
{
   int rc = -ENOENT; /* default, kind-of random, error */
   page_directory_t *pdir = NULL;
   char *filename_copy = NULL;
   char *const *argv_copy = NULL;
   char *const *env_copy = NULL;
   void *entry_point;
   void *stack_addr;

   filename_copy = strdup(filename);

   if (filename && !filename_copy) {
      rc = -ENOMEM;
      goto errend2;
   }

   argv_copy = dcopy_strarray(argv);

   if (argv && !argv_copy) {
      rc = -ENOMEM;
      goto errend2;
   }

   env_copy = dcopy_strarray(env);

   if (env && !env_copy) {
      rc = -ENOMEM;
      goto errend2;
   }

   disable_preemption();

   rc = load_elf_program(filename, &pdir, &entry_point, &stack_addr);

   if (rc < 0) {
      goto errend;
   }

   char *const default_argv[] = { filename_copy, NULL };

   if (LIKELY(current != NULL)) {
      task_change_state(current, TASK_STATE_RUNNABLE);
      pdir_destroy(current->pi->pdir);
   }

   create_usermode_task(pdir,
                        entry_point,
                        stack_addr,
                        current,
                        argv_copy ? argv_copy : default_argv,
                        env_copy ? env_copy : default_env);

   /* Free the duplicated buffers */
   kfree(filename_copy);
   dfree_strarray(argv_copy);
   dfree_strarray(env_copy);

   if (UNLIKELY(!current)) {

      /* Just counter-balance the disable_preemption() above */
      enable_preemption();

      /* We're still in the initialization and preemption is disabled */
      ASSERT(!is_preemption_enabled());

      /* Compensate the pop_nested_interrupt() in switch_to_task() */
      push_nested_interrupt(-1);
   }

   switch_to_idle_task();
   /* this point is unreachable */

errend:
   enable_preemption();

errend2:
   VERIFY(rc != 0);

   /* Free the duplicated buffers */
   kfree(filename_copy);
   dfree_strarray(argv_copy);
   dfree_strarray(env_copy);
   return rc;
}

sptr sys_pause()
{
   task_change_state(current, TASK_STATE_SLEEPING);
   kernel_yield();
   return 0;
}

sptr sys_getpid()
{
   ASSERT(current != NULL);
   return current->owning_process_pid;
}

sptr sys_waitpid(int pid, int *wstatus, int options)
{
   ASSERT(are_interrupts_enabled());

   // printk("Pid %i will WAIT until pid %i dies\n",
   //        current->pid, pid);

   ASSERT(are_interrupts_enabled());
   DEBUG_VALIDATE_STACK_PTR();

   if (pid > 0) {

      /* Wait for a specific PID */

      volatile task_info *waited_task = (volatile task_info *)get_task(pid);

      if (!waited_task) {
         return -ECHILD;
      }

      while (waited_task->state != TASK_STATE_ZOMBIE) {
         wait_obj_set(&current->wobj, WOBJ_PID, (task_info *)waited_task);
         task_change_state(current, TASK_STATE_SLEEPING);
         kernel_yield();
      }

      if (wstatus) {
         *wstatus = EXITCODE(waited_task->exit_status, 0);
      }

      remove_task((task_info *)waited_task);
      return pid;

   } else {

      /*
       * Since exOS does not support UIDs and GIDs != 0, the values of
       *    pid < -1
       *    pid == -1
       *    pid == 0
       * are treated in the same way.
       */

      NOT_IMPLEMENTED();
   }
}

NORETURN void sys_exit(int exit_status)
{
   disable_preemption();

   // printk("Exit process %i with code = %i\n",
   //        current->pid,
   //        exit_status);

   task_change_state(current, TASK_STATE_ZOMBIE);
   current->exit_status = exit_status;

   // Close all of its opened handles

   for (size_t i = 0; i < ARRAY_SIZE(current->pi->handles); i++) {

      fs_handle *h = current->pi->handles[i];

      if (h) {
         exvfs_close(h);
         current->pi->handles[i] = NULL;
      }
   }


   // Wake-up all the tasks waiting on this task to exit

   task_info *pos;

   list_for_each(pos, &sleeping_tasks_list, sleeping_list) {

      ASSERT(pos->state == TASK_STATE_SLEEPING);

      if (pos->wobj.ptr == current) {
         ASSERT(pos->wobj.type == WOBJ_PID);
         wait_obj_reset(&pos->wobj);
         task_change_state(pos, TASK_STATE_RUNNABLE);
      }
   }

   set_page_directory(get_kernel_page_dir());
   pdir_destroy(current->pi->pdir);

#ifdef DEBUG_QEMU_EXIT_ON_INIT_EXIT
   if (current->tid == 1) {
      debug_qemu_turn_off_machine();
   }
#endif

   /* WARNING: the following call discards the whole stack! */
   switch_to_initial_kernel_stack();

   /* Free the heap allocations used by the task, including the kernel stack */
   free_mem_for_zombie_task(current);
   schedule();

   /* Necessary to guarantee to the compiler that we won't return. */
   NOT_REACHED();
}

// Returns child's pid
sptr sys_fork(void)
{
   disable_preemption();
   //printk("heap allocation: %u bytes\n", kmalloc_get_total_heap_allocation());

   int pid = create_new_pid();

   if (pid == -1) {
      enable_preemption();
      return -EAGAIN;
   }

   task_info *child = allocate_new_process(current, pid);
   VERIFY(child != NULL); // TODO: handle this

   if (child->state == TASK_STATE_RUNNING) {
      child->state = TASK_STATE_RUNNABLE;
   }

   child->pi->pdir = pdir_clone(current->pi->pdir);
   child->running_in_kernel = false;
   ASSERT(child->kernel_stack != NULL);
   task_info_reset_kernel_stack(child);
   set_return_register(&child->state_regs, 0);
   add_task(child);

   // Make the parent to get child's pid as return value.
   set_return_register(&current->state_regs, child->tid);

   /* Duplicate all the handles */
   for (size_t i = 0; i < ARRAY_SIZE(child->pi->handles); i++) {
      if (child->pi->handles[i])
         child->pi->handles[i] = exvfs_dup(child->pi->handles[i]);
   }

   /*
    * Force the CR3 reflush using the current (parent's) pdir.
    * Without doing that, COW on parent's pages doesn't work immediately.
    * That is better (in this case) than invalidating all the pages affected,
    * one by one.
    */

   set_page_directory(current->pi->pdir);

   enable_preemption();
   return child->tid;
}
