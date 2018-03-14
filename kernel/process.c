#include <list.h>
#include <kmalloc.h>
#include <string_util.h>
#include <process.h>
#include <hal.h>

//#define DEBUG_printk printk
#define DEBUG_printk(...)

extern task_info *volatile current;
extern int current_max_pid;


/*
 * ***************************************************************
 *
 * SYSCALLS
 *
 * ***************************************************************
 */

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

   printk("[kernel] Pid %i will WAIT until pid %i dies\n",
          current->pid, pid);

   ASSERT(are_interrupts_enabled());

   volatile task_info *waited_task = (volatile task_info *)get_task(pid);

   if (!waited_task) {
      return -1;
   }

   DEBUG_VALIDATE_STACK_PTR();

   /*
    * This is just a DEMO implementation of waitpid() having the goal
    * to test the preemption of kernel code running for user applications.
    */

   while (true) {

      if (waited_task->state == TASK_STATE_ZOMBIE) {
         break;
      }

      ASSERT(are_interrupts_enabled());
      halt();

      DEBUG_VALIDATE_STACK_PTR();
   }

   remove_task((task_info *)waited_task);
   return pid;
}


#ifdef DEBUG_QEMU_EXIT_ON_INIT_EXIT
extern task_info *usermode_init_task;
#endif

NORETURN void sys_exit(int exit_code)
{
   disable_preemption();

   printk("[kernel] Exit process %i with code = %i\n",
          current->pid,
          exit_code);

   task_change_state(current, TASK_STATE_ZOMBIE);
   current->exit_code = exit_code;

   // Close all of its opened handles

   for (size_t i = 0; i < ARRAY_SIZE(current->handles); i++) {

      fs_handle *h = current->handles[i];

      if (h)
         exvfs_close(h);
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
sptr sys_fork()
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
   child->running_in_kernel = 0;

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
