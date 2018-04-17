
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/list.h>
#include <exos/kmalloc.h>
#include <exos/process.h>
#include <exos/hal.h>
#include <exos/elf_loader.h>
#include <exos/errno.h>
#include <exos/user.h>

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
   task_info *curr = get_curr_task();
   process_info *pi = curr->pi;
   const size_t max_size = ARRAY_SIZE(pi->cwd);

   disable_preemption();
   {
      rc = copy_str_from_user(pi->cwd, path, max_size);

      if (rc < 0) {
         rc = -EFAULT;
         goto out;
      }

      if (rc > 0) {
         rc = -ENAMETOOLONG;
         goto out;
      }
   }

out:
   enable_preemption();
   return rc;
}

sptr sys_getcwd(char *buf, size_t buf_size)
{
   sptr ret;
   size_t cwd_len;
   disable_preemption();
   {
      cwd_len = strlen(get_curr_task()->pi->cwd) + 1;

      if (!buf || !buf_size) {
         ret = -EINVAL;
         goto out;
      }

      if (buf_size < cwd_len) {
         ret = -ERANGE;
         goto out;
      }

      ret = copy_to_user(buf, get_curr_task()->pi->cwd, cwd_len);

      if (ret < 0) {
         ret = -EFAULT;
         goto out;
      }

      ret = cwd_len;
   }

out:
   enable_preemption();
   return ret;
}

static int duplicate_user_path(char *dest,
                               const char *user_path,
                               size_t dest_size,
                               size_t *written_ptr)
{
   int rc;

   if (!user_path)
      return -EINVAL;

   rc = copy_str_from_user(dest + *written_ptr,
                           user_path,
                           dest_size - *written_ptr);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -ENAMETOOLONG;

   *written_ptr += strlen(user_path) + 1;
   return 0;
}

static int duplicate_user_argv(char *dest,
                               const char *const *user_argv,
                               size_t dest_size,
                               size_t *written_ptr /* IN/OUT */)
{
   int rc;

   rc = copy_str_array_from_user(dest + *written_ptr,
                                 user_argv,
                                 dest_size - *written_ptr,
                                 written_ptr);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -E2BIG;

   return 0;
}

sptr sys_execve(const char *user_filename,
                const char *const *user_argv,
                const char *const *user_env)
{
   int rc = -ENOENT; /* default, kind-of random, error */
   page_directory_t *pdir = NULL;
   char *filename_copy = NULL;
   char *const *argv_copy = NULL;
   char *const *env_copy = NULL;
   task_info *curr = get_curr_task();
   void *entry_point;
   void *stack_addr;

   disable_preemption();

   if (LIKELY(curr != NULL)) {

      char *dest = (char *)curr->args_copybuf;
      size_t written = 0;

      filename_copy = dest;
      rc = duplicate_user_path(dest,
                               user_filename,
                               MIN(MAX_PATH, ARGS_COPYBUF_SIZE),
                               &written);

      if (rc != 0)
         goto errend;

      written += strlen(filename_copy) + 1;

      if (user_argv) {
         argv_copy = (char *const *) (dest + written);
         rc = duplicate_user_argv(dest,
                                  user_argv,
                                  ARGS_COPYBUF_SIZE,
                                  &written);
         if (rc != 0)
            goto errend;
      }

      if (user_env) {
         env_copy = (char *const *) (dest + written);
         rc = duplicate_user_argv(dest,
                                  user_env,
                                  ARGS_COPYBUF_SIZE,
                                  &written);
         if (rc != 0)
            goto errend;
      }


   } else {

      filename_copy = (char *) user_filename;
      argv_copy = (char *const *) user_argv;
      env_copy = (char *const *) user_env;
   }

   rc = load_elf_program(filename_copy, &pdir, &entry_point, &stack_addr);

   if (rc < 0) {
      goto errend;
   }

   char *const default_argv[] = { filename_copy, NULL };

   if (LIKELY(curr != NULL)) {
      task_change_state(curr, TASK_STATE_RUNNABLE);
      pdir_destroy(curr->pi->pdir);
   }

   create_usermode_task(pdir,
                        entry_point,
                        stack_addr,
                        curr,
                        argv_copy ? argv_copy : default_argv,
                        env_copy ? env_copy : default_env);

   if (UNLIKELY(!curr)) {

      /* Just counter-balance the disable_preemption() above */
      enable_preemption();

      /* We're still in the initialization and preemption is disabled */
      ASSERT(!is_preemption_enabled());

      /* Compensate the pop_nested_interrupt() in switch_to_task() */
      push_nested_interrupt(-1);
   }

   switch_to_idle_task();
   NOT_REACHED();

errend:
   enable_preemption();
   VERIFY(rc != 0);
   return rc;
}

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
                      WOBJ_PID,
                      (task_info *)waited_task);
         task_change_state(get_curr_task(), TASK_STATE_SLEEPING);
         kernel_yield();
      }

      if (wstatus) {

         disable_preemption();
         {
            if (check_user_ptr_size_writable(wstatus) < 0)
               return -EFAULT;
         }
         enable_preemption();

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
         exvfs_close(h);
         curr->pi->handles[i] = NULL;
      }
   }


   // Wake-up all the tasks waiting on this task to exit

   task_info *pos;

   list_for_each(pos, &sleeping_tasks_list, sleeping_list) {

      ASSERT(pos->state == TASK_STATE_SLEEPING);

      if (pos->wobj.ptr == curr) {
         ASSERT(pos->wobj.type == WOBJ_PID);
         wait_obj_reset(&pos->wobj);
         task_change_state(pos, TASK_STATE_RUNNABLE);
      }
   }

   set_page_directory(get_kernel_page_dir());
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
   schedule();

   /* Necessary to guarantee to the compiler that we won't return. */
   NOT_REACHED();
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
   VERIFY(child != NULL); // TODO: handle this

   if (child->state == TASK_STATE_RUNNING) {
      child->state = TASK_STATE_RUNNABLE;
   }

   child->pi->pdir = pdir_clone(curr->pi->pdir);
   child->running_in_kernel = false;
   ASSERT(child->kernel_stack != NULL);
   task_info_reset_kernel_stack(child);
   set_return_register(&child->state_regs, 0);
   add_task(child);

   // Make the parent to get child's pid as return value.
   set_return_register(&curr->state_regs, child->tid);

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

   set_page_directory(curr->pi->pdir);

   enable_preemption();
   return child->tid;
}
