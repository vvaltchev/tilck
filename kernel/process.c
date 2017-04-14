
#include <list.h>
#include <kmalloc.h>
#include <string_util.h>

#include <arch_utils.h>

#define TIME_SLOT_JIFFIES 50

task_info *volatile current_task = NULL;
int current_max_pid = 0;

// Our linked list for all the tasks (processes, threads, etc.)
LIST_HEAD(tasks_list);


task_info *get_current_task()
{
   return current_task;
}


bool is_kernel_thread(task_info *ti)
{
   //return ti->task_process_pid == 0;

   if (ti->owning_process_pid != ti->pid) {
      printk("own pid (%i) != pid (%i)\n", ti->owning_process_pid, ti->pid);
      NOT_REACHED();
   }

   return ti->is_kthread;
}


void save_current_task_state(regs *r)
{
   memmove(&current_task->state_regs, r, sizeof(*r));

   if (is_kernel_thread(current_task)) {
      /*
       * If the current task is a kernel thread, than the useresp has not
       * be saved on the stack by the CPU, since there has been not priviledge
       * change. So, we have to use the actual value of ESP as 'useresp' and
       * adjust it by +16. That's why when the interrupt occured, the CPU
       * pushed on the stack CS+EIP and we pushed int_num + err_code; in total,
       * 4 pointer-size integers.
       */
      current_task->state_regs.useresp = r->esp + 16;
   }
}

void add_task(task_info *ti)
{
   ti->state = TASK_STATE_RUNNABLE;
   list_add_tail(&tasks_list, &ti->list);
}

void remove_task(task_info *ti)
{
   printk("[remove_task] pid = %i\n", ti->pid);
   list_remove(&ti->list);
   kfree(ti, sizeof(task_info));
}

NORETURN void switch_to_task(task_info *pi)
{
   ASSERT(pi->state == TASK_STATE_RUNNABLE);

   pi->state = TASK_STATE_RUNNING;

   if (get_curr_page_dir() != pi->pdir) {
      set_page_directory(pi->pdir);
   }

   end_current_interrupt_handling();
   current_task = pi;


   // This allows the switch to happen without interrupts.
   disable_preemption_for(TIMER_HZ / 10);
   irq_clear_mask(X86_PC_TIMER_IRQ);

   if (!is_kernel_thread(current_task)) {
      context_switch(&current_task->state_regs);
   } else {
      kthread_context_switch(&current_task->state_regs);
   }
}


NORETURN void schedule()
{
   task_info *curr = current_task;
   task_info *selected = curr;
   task_info *pos;
   const u64 jiffies_used = jiffies - curr->jiffies_when_switch;

   if (curr->state == TASK_STATE_ZOMBIE && is_kernel_thread(curr)) {
      // We're dealing with a dead tasklet
      // TODO: this code has to be fixed since we cannot free kthread's stack
      // while we're using it.
      remove_task(curr);
      curr = NULL;
      goto actual_sched;
   }

   if (jiffies_used < TIME_SLOT_JIFFIES && curr->state == TASK_STATE_RUNNING) {
      curr->state = TASK_STATE_RUNNABLE;
      goto end;
   }

   printk("[sched] Current pid: %i, used %llu jiffies\n",
          current_task->pid, jiffies_used);

   // If we preempted the process, it is still runnable.
   if (curr->state == TASK_STATE_RUNNING) {
      curr->state = TASK_STATE_RUNNABLE;
   }

   // Actual scheduling logic.
actual_sched:

   list_for_each_entry(pos, &tasks_list, list) {
      if (pos != curr && pos->state == TASK_STATE_RUNNABLE) {
         selected = pos;
         break;
      }
   }

   selected->jiffies_when_switch = jiffies;

   // Finalizing code.

end:

   if (selected->state != TASK_STATE_RUNNABLE) {

      printk("[sched] No runnable process found. Halt.\n");

      end_current_interrupt_handling();

      // Re-enable the timer.
      irq_clear_mask(X86_PC_TIMER_IRQ);

      // We did not found any runnable task. Halt.
      halt();
   }

   if (selected != curr) {
      printk("[sched] Switching to pid: %i %s\n",
             selected->pid, is_kernel_thread(selected) ? "[KTHREAD]" : "");
   }

   ASSERT(selected != NULL);
   switch_to_task(selected);
}



/*
 * ***************************************************************
 *
 * SYSCALLS
 *
 * ***************************************************************
 */

int sys_getpid()
{
   ASSERT(current_task != NULL);
   return current_task->pid;
}

NORETURN void sys_exit(int exit_code)
{
   printk("[kernel] Exit process %i with code = %i\n",
          current_task->pid,
          exit_code);

   current_task->state = TASK_STATE_ZOMBIE;
   current_task->exit_code = exit_code;
   pdir_destroy(current_task->pdir);
   schedule();
}

// Returns child's pid
int sys_fork()
{
   page_directory_t *pdir = pdir_clone(current_task->pdir);

   task_info *child = kmalloc(sizeof(task_info));
   INIT_LIST_HEAD(&child->list);
   child->pdir = pdir;
   child->pid = ++current_max_pid;
   child->is_kthread = false;
   child->owning_process_pid = child->pid;

   memmove(&child->state_regs,
           &current_task->state_regs,
           sizeof(child->state_regs));

   set_return_register(&child->state_regs, 0);


   //printk("forking current proccess with eip = %p\n", child->state_regs.eip);

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
   return child->pid;
}
