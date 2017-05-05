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

void set_current_task_in_kernel()
{
   ASSERT(!is_preemption_enabled());
   current_task->running_in_kernel = 1;
}

void set_current_task_in_user_mode()
{
   ASSERT(!is_preemption_enabled());

   current_task->running_in_kernel = 0;

   reset_kernel_stack(current_task);
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
         printk("[kernel] PREEMPTING kernel code for user program!\n");
      }
   }
}

void add_task(task_info *ti)
{
   ti->state = TASK_STATE_RUNNABLE;
   list_add_tail(&tasks_list, &ti->list);
}

void remove_task(task_info *ti)
{
   ASSERT(ti->state == TASK_STATE_ZOMBIE);

   disable_preemption();
   {
      printk("[remove_task] pid = %i\n", ti->pid);
      list_remove(&ti->list);

      kfree(ti->kernel_stack, KTHREAD_STACK_SIZE);
      kfree(ti, sizeof(task_info));
   }
   enable_preemption();
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

      reset_kernel_stack(current_task);
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

   printk("\n\n[sched] Current pid: %i, used %llu ticks (%llu in kernel)\n",
          current_task->pid, curr->total_ticks, curr->kernel_ticks);

   return true;
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
      printk("[sched] Switching to pid: %i %s %s\n",
             selected->pid,
             is_kernel_thread(selected) ? "[KTHREAD]" : "[USER]",
             selected->running_in_kernel ? "(kernel mode)" : "(usermode)");
   }

   ASSERT(selected != NULL);
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
         goto end;
      }
   }

end:
   enable_preemption();
   return res;
}

