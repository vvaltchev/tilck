#include <list.h>
#include <kmalloc.h>
#include <string_util.h>

#include <hal.h>

//#define DEBUG_printk printk
#define DEBUG_printk(...)

extern task_info *volatile current_task;
extern int current_max_pid;


/*
 * ***************************************************************
 *
 * SYSCALLS
 *
 * ***************************************************************
 */

sptr sys_getpid()
{
   ASSERT(current_task != NULL);
   return current_task->pid;
}

sptr sys_waitpid(int pid, int *wstatus, int options)
{
   printk("[kernel] Pid %i will WAIT until pid %i dies\n",
          current_task->pid, pid);

   task_info *waited_task = get_task(pid);

   if (!waited_task) {
      return -1;
   }

   /*
    * This is just a DEMO implementation of waitpid() having the goal
    * to test the preemption of kernel code running for user applications.
    */

   while (true) {

      if (waited_task->state == TASK_STATE_ZOMBIE) {
         break;
      }

      halt();
   }

   remove_task(waited_task);
   return pid;
}


NORETURN void sys_exit(int exit_code)
{
   disable_preemption();

   printk("[kernel] Exit process %i with code = %i\n",
          current_task->pid,
          exit_code);

   task_change_state(current_task, TASK_STATE_ZOMBIE);
   current_task->exit_code = exit_code;

   // We CANNOT free current_task->kernel_task here because we're using it!

   pdir_destroy(current_task->pdir);
   schedule();
}

// Returns child's pid
sptr sys_fork()
{
   disable_preemption();

   task_info *child = kmalloc(sizeof(task_info));
   memmove(child, current_task, sizeof(task_info));

   INIT_LIST_HEAD(&child->list);
   child->pdir = pdir_clone(current_task->pdir);
   child->pid = ++current_max_pid;

   child->owning_process_pid = child->pid;
   child->running_in_kernel = 0;

   // The other members of task_info have been copied by the memmove() above
   bzero(&child->kernel_state_regs, sizeof(child->kernel_state_regs));

   child->kernel_stack = kmalloc(KTHREAD_STACK_SIZE);
   bzero(child->kernel_stack, KTHREAD_STACK_SIZE);
   task_info_reset_kernel_stack(child);

   set_return_register(&child->state_regs, 0);

   add_task(child);

   // Make the parent to get child's pid as return value.
   set_return_register(&current_task->state_regs, child->pid);

   /*
    * Force the CR3 reflush using the current (parent's) pdir.
    * Without doing that, COW on parent's pages doesn't work immediately.
    * That is better (in this case) than invalidating all the pages affected,
    * one by one.
    */

   set_page_directory(current_task->pdir);

   enable_preemption();
   return child->pid;
}
