#include <list.h>
#include <kmalloc.h>
#include <string_util.h>

#include <hal.h>

//#define DEBUG_printk printk
#define DEBUG_printk(...)


#define TIME_SLOT_JIFFIES (TIMER_HZ * 3)
//#define TIME_SLOT_JIFFIES (TIMER_HZ / 50)

task_info *volatile current_task = NULL;
int current_max_pid = 0;

// Our linked list for all the tasks (processes, threads, etc.)
list_head tasks_list = LIST_HEAD_INIT(tasks_list);
list_head runnable_tasks_list = LIST_HEAD_INIT(runnable_tasks_list);
list_head sleeping_tasks_list = LIST_HEAD_INIT(sleeping_tasks_list);


/*
 * TODO: consider implementing a mechanism such that the idle thread is runnable
 * only when there are actually no runnable processes, in order to minimize
 * the time wasted in scheduling it.
 */

void idle_task_kthread()
{
   while (true) {
      halt();
      kernel_yield();
   }
}

void initialize_scheduler(void)
{
   current_task = kthread_create(&idle_task_kthread, NULL);
}

bool is_kernel_thread(task_info *ti)
{
   return ti->owning_process_pid == 0;
}

void set_current_task_in_kernel()
{
   ASSERT(!is_preemption_enabled());
   current_task->running_in_kernel = 1;
}

void set_current_task_in_user_mode()
{
   ASSERT(!is_preemption_enabled());

   current_task->running_in_kernel = 0;

   task_info_reset_kernel_stack(current_task);
   set_kernel_stack(current_task->kernel_state_regs.useresp);
}


void save_current_task_state(regs *r)
{
   regs *state = current_task->running_in_kernel
                    ? &current_task->kernel_state_regs
                    : &current_task->state_regs;

   memmove(state, r, sizeof(*r));

   if (current_task->running_in_kernel) {

      /*
       * If the current task was running in kernel, than the useresp has not
       * be saved on the stack by the CPU, since there has been not priviledge
       * change. So, we have to use the actual value of ESP as 'useresp' and
       * adjust it by +16. That's because when the interrupt occured, the CPU
       * pushed on the stack CS+EIP and we pushed int_num + err_code; in total,
       * 4 pointer-size integers.
       */
      state->useresp = r->esp + 16;

      state->eflags = get_eflags();
      state->ss = 0x10;

      if (!is_kernel_thread(current_task)) {
         DEBUG_printk("[kernel] PREEMPTING kernel code for user program!\n");
      }
   }
}

void task_add_to_state_list(task_info *ti)
{
   switch (ti->state) {

   case TASK_STATE_RUNNABLE:
      list_add_tail(&runnable_tasks_list, &ti->runnable_list);
      break;

   case TASK_STATE_SLEEPING:
      list_add_tail(&sleeping_tasks_list, &ti->sleeping_list);
      break;

   case TASK_STATE_RUNNING:
   case TASK_STATE_ZOMBIE:
      break;

   default:
      NOT_REACHED();
   }
}

void task_remove_from_state_list(task_info *ti)
{
   switch (ti->state) {

   case TASK_STATE_RUNNABLE:
      list_remove(&ti->runnable_list);
      break;

   case TASK_STATE_SLEEPING:
      list_remove(&ti->sleeping_list);
      break;

   case TASK_STATE_RUNNING:
   case TASK_STATE_ZOMBIE:
      break;

   default:
      NOT_REACHED();
   }
}

void task_change_state(task_info *ti, int new_state)
{
   disable_preemption();
   {
      ASSERT(ti->state != new_state);
      task_remove_from_state_list(ti);

      ti->state = new_state;

      task_add_to_state_list(ti);
   }
   enable_preemption();
}

void add_task(task_info *ti)
{
   disable_preemption();
   {
      list_add_tail(&tasks_list, &ti->list);
      task_add_to_state_list(ti);
   }
   enable_preemption();
}

void remove_task(task_info *ti)
{
   disable_preemption();
   {
      ASSERT(ti->state == TASK_STATE_ZOMBIE);
      DEBUG_printk("[remove_task] pid = %i\n", ti->pid);

      task_remove_from_state_list(ti);
      list_remove(&ti->list);

      kfree(ti->kernel_stack, KTHREAD_STACK_SIZE);
      kfree(ti, sizeof(task_info));
   }
   enable_preemption();
}

NORETURN void switch_to_task(task_info *ti)
{
   ASSERT(ti->state == TASK_STATE_RUNNABLE);

   task_change_state(ti, TASK_STATE_RUNNING);
   ti->ticks = 0;

   if (get_curr_page_dir() != ti->pdir) {
      set_page_directory(ti->pdir);
   }

   disable_interrupts();

   // We have to be SURE that the timer IRQ is NOT masked!
   irq_clear_mask(X86_PC_TIMER_IRQ);

   end_current_interrupt_handling();

   if (current_task &&
       current_task->running_in_kernel && !is_kernel_thread(current_task)) {

      if (nested_interrupts_count > 0) {

         ASSERT(nested_interrupts_count == 1);
         ASSERT(get_curr_interrupt() == 0x80); // int 0x80 (syscall)
         end_current_interrupt_handling();
      }
   }

   enable_preemption();

   ASSERT(is_preemption_enabled());

   current_task = ti;

   regs *state = current_task->running_in_kernel
                    ? &current_task->kernel_state_regs
                    : &current_task->state_regs;

   /*
    * ASSERT that the 9th bit in task's eflags is 1, which means that on
    * IRET the CPU will enable the interrupts.
    */

   ASSERT(state->eflags & (1 << 9));


   if (!current_task->running_in_kernel) {

      bzero(current_task->kernel_stack, KTHREAD_STACK_SIZE);

      task_info_reset_kernel_stack(current_task);
      set_kernel_stack(current_task->kernel_state_regs.useresp);

      context_switch(state);

   } else {

      if (!is_kernel_thread(current_task)) {
         push_nested_interrupt(0x80);
      }

      kernel_context_switch(state);
   }
}

void account_ticks()
{
   if (!current_task) {
      return;
   }

   current_task->ticks++;
   current_task->total_ticks++;

   if (current_task->running_in_kernel) {
      current_task->kernel_ticks++;
   }
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

   DEBUG_printk("\n\n[sched] Current pid: %i, "
                "used %llu ticks (%llu in kernel)\n",
                current_task->pid, curr->total_ticks, curr->kernel_ticks);

   return true;
}

NORETURN void schedule_outside_interrupt_context()
{
   // HACK: push a fake interrupt to compensate the call to
   // end_current_interrupt_handling() in switch_to_process().

   push_nested_interrupt(-1);
   schedule();
}


NORETURN void schedule()
{
   task_info *curr = current_task;
   task_info *selected = curr;
   task_info *pos;
   u64 least_ticks_for_task = (u64)-1;

   ASSERT(!is_preemption_enabled());

   if (curr->state == TASK_STATE_ZOMBIE && is_kernel_thread(curr)) {
      remove_task(curr);
      selected = curr = NULL;
      goto actual_sched;
   }


   // If we preempted the process, it is still runnable.
   if (curr->state == TASK_STATE_RUNNING) {
      task_change_state(curr, TASK_STATE_RUNNABLE);
   }

   // Actual scheduling logic.
actual_sched:

   list_for_each_entry(pos, &runnable_tasks_list, runnable_list) {

      DEBUG_printk("   [sched] checking pid %i (ticks = %llu): ",
                   pos->pid, pos->total_ticks);

      ASSERT(pos->state == TASK_STATE_RUNNABLE);

      if (pos == curr) {
         DEBUG_printk("SKIP\n");
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

   ASSERT(selected != NULL);

   if (selected != curr) {
      DEBUG_printk("[sched] Switching to pid: %i %s %s\n",
             selected->pid,
             is_kernel_thread(selected) ? "[KTHREAD]" : "[USER]",
             selected->running_in_kernel ? "(kernel mode)" : "(usermode)");
   }

   switch_to_task(selected);
}

// TODO: make this function much faster (e.g. indexing by pid)
task_info *get_task(int pid)
{
   task_info *pos;
   task_info *res = NULL;

   disable_preemption();

   list_for_each_entry(pos, &tasks_list, list) {
      if (pos->pid == pid) {
         res = pos;
         break;
      }
   }

   enable_preemption();
   return res;
}

