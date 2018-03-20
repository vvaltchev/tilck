#include <list.h>
#include <kmalloc.h>
#include <string_util.h>
#include <process.h>
#include <hal.h>
#include <elf_loader.h>
#include <exos_errno.h>

//#define DEBUG_printk printk
#define DEBUG_printk(...)

extern task_info *current;
extern int current_max_pid;

#define EXITCODE(ret, sig)  ((ret) << 8 | (sig))
#define STOPCODE(sig) ((sig) << 8 | 0x7f)
#define CONTINUED 0xffff
#define COREFLAG 0x80

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

      if (path_len > ARRAY_SIZE(current->cwd)) {
         rc = -ENAMETOOLONG;
         goto out;
      }

      memmove(current->cwd, path, path_len);
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
      cwd_len = strlen(current->cwd) + 1;

      if (!buf)
         return -EINVAL;

      if (buf_size < cwd_len)
         return -ERANGE;

      memmove(buf, current->cwd, cwd_len);
   }
   enable_preemption();
   return cwd_len;
}

sptr sys_execve(const char *filename,
                const char *const *argv,
                const char *const *env)
{
   int rc = -ENOENT; /* default, kind-of random, error */

   disable_preemption();
   {
      void *entry_point = NULL;
      void *stack_addr = NULL;
      page_directory_t *pdir = NULL;

      char *filename_copy = strdup(filename);
      char *const *argv_copy = dcopy_strarray(argv);
      char *const *env_copy = dcopy_strarray(env);

      rc = load_elf_program(filename, &pdir, &entry_point, &stack_addr);

      if (rc < 0)
         goto errend;

      char *const default_argv[] = { filename_copy, NULL };

      task_change_state(current, TASK_STATE_RUNNABLE);
      pdir_destroy(current->pdir);

      create_usermode_task(pdir,
                           entry_point,
                           stack_addr,
                           current,
                           argv_copy ? argv_copy : default_argv,
                           env_copy ? env_copy : default_env);

      /* Free the duplicated buffers */
      kfree(filename_copy, strlen(filename_copy) + 1);
      dfree_strarray(argv_copy);
      dfree_strarray(env_copy);

      /* Remove the "int 0x80" from the nested_interrupts stack */
      pop_nested_interrupt();

      /* Switch to the idle task */
      switch_to_idle_task_outside_interrupt_context();
    }

errend:
   enable_preemption();
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
   return current->pid;
}

sptr sys_waitpid(int pid, int *wstatus, int options)
{
   ASSERT(are_interrupts_enabled());

   // printk("[kernel] Pid %i will WAIT until pid %i dies\n",
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


#ifdef DEBUG_QEMU_EXIT_ON_INIT_EXIT
extern task_info *usermode_init_task;
#endif

NORETURN void sys_exit(int exit_status)
{
   disable_preemption();

   // printk("[kernel] Exit process %i with code = %i\n",
   //        current->pid,
   //        exit_status);

   task_change_state(current, TASK_STATE_ZOMBIE);
   current->exit_status = exit_status;

   // Close all of its opened handles

   for (size_t i = 0; i < ARRAY_SIZE(current->handles); i++) {

      fs_handle *h = current->handles[i];

      if (h)
         exvfs_close(h);
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

   // We CANNOT free current->kernel_task here because we're using it!

   set_page_directory(get_kernel_page_dir());
   pdir_destroy(current->pdir);

#ifdef DEBUG_QEMU_EXIT_ON_INIT_EXIT
   if (current == usermode_init_task) {
      debug_qemu_turn_off_machine();
   }
#endif

   schedule();
   NOT_REACHED();
}

// Returns child's pid
sptr sys_fork(void)
{
   disable_preemption();

   task_info *child = kmalloc(sizeof(task_info));
   memmove(child, current, sizeof(task_info));

   list_node_init(&child->list);

   if (child->state == TASK_STATE_RUNNING) {
      child->state = TASK_STATE_RUNNABLE;
   }

   child->pdir = pdir_clone(current->pdir);
   child->pid = ++current_max_pid;

   child->owning_process_pid = child->pid;
   child->running_in_kernel = false;
   child->parent_pid = current->pid;

   // The other members of task_info have been copied by the memmove() above
   bzero(&child->kernel_state_regs, sizeof(child->kernel_state_regs));

   child->kernel_stack = kmalloc(KTHREAD_STACK_SIZE);
   bzero(child->kernel_stack, KTHREAD_STACK_SIZE);
   task_info_reset_kernel_stack(child);

   set_return_register(&child->state_regs, 0);

   add_task(child);

   // Make the parent to get child's pid as return value.
   set_return_register(&current->state_regs, child->pid);

   /*
    * Force the CR3 reflush using the current (parent's) pdir.
    * Without doing that, COW on parent's pages doesn't work immediately.
    * That is better (in this case) than invalidating all the pages affected,
    * one by one.
    */

   set_page_directory(current->pdir);

   enable_preemption();
   return child->pid;
}
