
#include <list.h>
#include <kmalloc.h>
#include <string_util.h>

#include <hal.h>

//#define DEBUG_printk printk
#define DEBUG_printk(...)



#define TIME_SLOT_JIFFIES (TIMER_HZ * 3)

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
   return ti->owning_process_pid == 0;
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

NORETURN void switch_to_task(task_info *ti)
{
   ASSERT(ti->state == TASK_STATE_RUNNABLE);

   ti->state = TASK_STATE_RUNNING;
   ti->ticks = 0;

   if (get_curr_page_dir() != ti->pdir) {
      set_page_directory(ti->pdir);
   }

   disable_interrupts();

   // We have to be SURE that the timer IRQ is NOT masked!
   irq_clear_mask(X86_PC_TIMER_IRQ);

   end_current_interrupt_handling();
   enable_preemption();

   ASSERT(is_preemption_enabled());

   current_task = ti;


   /*
    * ASSERT that the 9th bit in task's eflags is 1, which means that on
    * IRET the CPU will enable the interrupts.
    */

   ASSERT(current_task->state_regs.eflags & (1 << 9));


   if (!is_kernel_thread(current_task)) {
      context_switch(&current_task->state_regs);
   } else {
      kthread_context_switch(&current_task->state_regs);
   }
}

void account_ticks()
{
   if (!current_task) {
      return;
   }

   current_task->ticks++;
   current_task->total_ticks++;
}

bool need_reschedule()
{
   task_info *curr = current_task;

   if (!curr) {
      // The kernel is still initializing and we cannot call schedule() yet.
      return false;
   }

   if (curr->ticks < TIME_SLOT_JIFFIES && curr->state == TASK_STATE_RUNNING) {
      return false;
   }

   DEBUG_printk("\n\n[sched] Current pid: %i, used %llu jiffies\n",
                current_task->pid, curr->total_ticks);

   return true;
}

NORETURN void schedule()
{
   task_info *curr = current_task;
   task_info *selected = curr;
   task_info *pos;
   u64 least_ticks_for_task = (u64)-1;

   if (curr->state == TASK_STATE_ZOMBIE && is_kernel_thread(curr)) {

      // Here we're able to free kthread's stack since we're using another stack
      kfree(curr->kernel_stack, KTHREAD_STACK_SIZE);

      remove_task(curr);
      selected = curr = NULL;
      goto actual_sched;
   }


   // If we preempted the process, it is still runnable.
   if (curr->state == TASK_STATE_RUNNING) {
      curr->state = TASK_STATE_RUNNABLE;
   }

   // Actual scheduling logic.
actual_sched:

   list_for_each_entry(pos, &tasks_list, list) {

       DEBUG_printk("   [sched] checking pid %i (ticks = %llu): ",
                    pos->pid, pos->total_ticks);

      if (pos == curr || pos->state != TASK_STATE_RUNNABLE) {

         if (pos == curr) {
            DEBUG_printk("SKIP\n");
         } else {
            DEBUG_printk("NOT RUNNABLE\n");
         }

         continue;
      }

      if (pos->total_ticks < least_ticks_for_task) {
         DEBUG_printk("GOOD\n");
         selected = pos;
         least_ticks_for_task = pos->total_ticks;
      } else {
         DEBUG_printk("BAD\n");
      }
   }

   // Finalizing code.

   if (selected->state != TASK_STATE_RUNNABLE) {

      printk("[sched] No runnable process found. Halt.\n");

      end_current_interrupt_handling();
      irq_clear_mask(X86_PC_TIMER_IRQ);
      enable_preemption();

      // We did not found any runnable task. Halt.
      halt();
   }

   if (selected != curr) {
      printk("[sched] Switching to pid: %i %s\n",
             selected->pid,
             is_kernel_thread(selected) ? "[KTHREAD]" : "[USER]");
   }

   ASSERT(selected != NULL);
   switch_to_task(selected);
}

task_info *get_task(int pid)
{
   task_info *pos;

   list_for_each_entry(pos, &tasks_list, list) {
      if (pos->pid == pid) {
         return pos;
      }
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
    * TODO: Finish this function.
    * In order make a syscall (= kernel code working for the user) to be
    * preemptable, syscalls need to have their own stack and context regs
    * in user's process data structure.
    */

   enable_preemption();
   {

   }
   disable_preemption();

   return waited_task->pid;
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
sptr sys_fork()
{
   page_directory_t *pdir = pdir_clone(current_task->pdir);

   task_info *child = kmalloc(sizeof(task_info));
   memmove(child, current_task, sizeof(task_info));

   INIT_LIST_HEAD(&child->list);
   child->pdir = pdir;
   child->pid = ++current_max_pid;

   child->owning_process_pid = child->pid;
   child->kernel_stack = NULL;

   child->ticks = current_task->ticks;
   child->total_ticks = current_task->total_ticks;

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
