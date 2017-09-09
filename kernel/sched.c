#include <list.h>
#include <kmalloc.h>
#include <string_util.h>

#include <hal.h>

//#define DEBUG_printk printk
#define DEBUG_printk(...)


// low debug value
//#define TIME_SLOT_JIFFIES (TIMER_HZ * 1)

// correct value (20 ms)
#define TIME_SLOT_JIFFIES (TIMER_HZ / 50)

// high debug value
//#define TIME_SLOT_JIFFIES (1)

task_info *volatile current = NULL;
int current_max_pid = 0;

// Our linked list for all the tasks (processes, threads, etc.)
list_node tasks_list = make_list_node(tasks_list);
list_node runnable_tasks_list = make_list_node(runnable_tasks_list);
list_node sleeping_tasks_list = make_list_node(sleeping_tasks_list);
volatile int runnable_tasks_count = 0;


task_info *idle_task = NULL;
volatile u64 idle_ticks = 0;

void idle_task_kthread()
{
   while (true) {

      idle_ticks++;
      halt();

      if (!(idle_ticks % TIMER_HZ)) {
         DEBUG_printk("[idle task] ticks: %llu\n", idle_ticks);
      }

      if (runnable_tasks_count > 0) {
         DEBUG_printk("[idle task] runnable > 0, yield!\n");
         kernel_yield();
      }
   }
}

void initialize_scheduler(void)
{
   idle_task = kthread_create(&idle_task_kthread, NULL);
}

bool is_kernel_thread(task_info *ti)
{
   return ti->owning_process_pid == 0;
}

void set_current_task_in_kernel()
{
   ASSERT(!is_preemption_enabled());
   current->running_in_kernel = 1;
}

void set_current_task_in_user_mode()
{
   ASSERT(!is_preemption_enabled());
   current->running_in_kernel = 0;

   task_info_reset_kernel_stack(current);
   set_kernel_stack(current->kernel_state_regs.useresp);
}


void save_current_task_state(regs *r)
{
   regs *state = current->running_in_kernel
                    ? &current->kernel_state_regs
                    : &current->state_regs;

   memmove(state, r, sizeof(*r));

   if (current->running_in_kernel) {

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

      if (!is_kernel_thread(current)) {
         DEBUG_printk("[kernel] PREEMPTING kernel code for user program!\n");
         DEBUG_VALIDATE_STACK_PTR();
      }
   }
}

void task_add_to_state_list(task_info *ti)
{
   switch (ti->state) {

   case TASK_STATE_RUNNABLE:
      list_add_tail(&runnable_tasks_list, &ti->runnable_list);
      runnable_tasks_count++;
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
      runnable_tasks_count--;
      ASSERT(runnable_tasks_count >= 0);
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

void task_change_state(task_info *ti, task_state_enum new_state)
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
   ASSERT(!is_preemption_enabled());
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
   ASSERT(!current || current->state != TASK_STATE_RUNNING);
   ASSERT(ti->state == TASK_STATE_RUNNABLE);
   ASSERT(ti != current);


   DEBUG_printk("[sched] Switching to pid: %i %s %s\n",
                ti->pid,
                is_kernel_thread(ti) ? "[KTHREAD]" : "[USER]",
                ti->running_in_kernel ? "(kernel mode)" : "(usermode)");

   task_change_state(ti, TASK_STATE_RUNNING);
   ti->ticks = 0;

   if (get_curr_page_dir() != ti->pdir) {
      set_page_directory(ti->pdir);
   }

   disable_interrupts_forced();

   ASSERT(disable_interrupts_count == 1);
   ASSERT(!are_interrupts_enabled());

   pop_nested_interrupt();

   if (current &&
       current->running_in_kernel && !is_kernel_thread(current)) {

      if (nested_interrupts_count > 0) {

         ASSERT(nested_interrupts_count == 1);
         ASSERT(nested_interrupts[0] == 0x80); // int 0x80 (syscall)
         pop_nested_interrupt();
      }
   }

   enable_preemption();
   ASSERT(is_preemption_enabled());

   regs *state = ti->running_in_kernel
                    ? &ti->kernel_state_regs
                    : &ti->state_regs;

   ASSERT(!are_interrupts_enabled());

   /*
    * The interrupts will be enabled after the context switch even if they are
    * disabled now, so only in this special context is OK to make that counter
    * equal to 0, without enabling the interrupts.
    */
   disable_interrupts_count = 0;

   DEBUG_VALIDATE_STACK_PTR();

   // We have to be SURE that the timer IRQ is NOT masked!
   irq_clear_mask(X86_PC_TIMER_IRQ);

   if (!ti->running_in_kernel) {

      bzero(ti->kernel_stack, KTHREAD_STACK_SIZE);

      task_info_reset_kernel_stack(ti);
      set_kernel_stack(ti->kernel_state_regs.useresp);

      /*
       * ASSERT that the 9th bit in task's eflags is 1, which means that on
       * IRET the CPU will enable the interrupts.
       */

      ASSERT(state->eflags & X86_EFLAGS_IF);

      current = ti;
      context_switch(state);

   } else {

      if (!is_kernel_thread(ti)) {
         push_nested_interrupt(0x80);
      }

      /*
       * Forcibily disable interrupts: that because we don't want them to be
       * automatically enabled when asm_kernel_context_switch_x86 runs POPF.
       * And that's because asm_kernel_context_switch_x86 has to POP ESP before
       * finally enabling them. See the comment in asm_kernel_context_switch_x86
       * for more about this.
       */
      state->eflags &= ~X86_EFLAGS_IF;

      set_kernel_stack(ti->kernel_state_regs.useresp);
      current = ti;
      kernel_context_switch(state);
   }
}

void account_ticks()
{
   if (!current) {
      return;
   }

   current->ticks++;
   current->total_ticks++;

   if (current->running_in_kernel) {
      current->kernel_ticks++;
   }
}

bool need_reschedule()
{
   if (!current) {
      // The kernel is still initializing and we cannot call schedule() yet.
      return false;
   }

   if (current->ticks < TIME_SLOT_JIFFIES &&
       current->state == TASK_STATE_RUNNING) {
      return false;
   }

   DEBUG_printk("\n\n[sched] Current pid: %i, "
                "used %llu ticks (user: %llu, kernel: %llu) [runnable: %i]\n",
                current->pid, current->ticks,
                current->total_ticks - current->kernel_ticks,
                current->kernel_ticks, runnable_tasks_count);

   return true;
}

void schedule_outside_interrupt_context()
{
   // HACK: push a fake interrupt to compensate the call to
   // pop_nested_interrupt() in switch_to_process().

   push_nested_interrupt(-1);
   schedule();
}

NORETURN void switch_to_idle_task_outside_interrupt_context()
{
   // HACK: push a fake interrupt to compensate the call to
   // pop_nested_interrupt() in switch_to_process().

   push_nested_interrupt(-1);
   switch_to_task(idle_task);
}


void schedule()
{
   task_info *selected = NULL;
   task_info *pos;
   u64 least_ticks_for_task = (u64)-1;

   ASSERT(!is_preemption_enabled());

   // If we preempted the process, it is still runnable.
   if (current->state == TASK_STATE_RUNNING) {
      task_change_state(current, TASK_STATE_RUNNABLE);
   }

   list_for_each(pos, &runnable_tasks_list, runnable_list) {

      DEBUG_printk("   [sched] checking pid %i (ticks = %llu): ",
                   pos->pid, pos->total_ticks);

      ASSERT(pos->state == TASK_STATE_RUNNABLE);

      if (pos == idle_task) {
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

   if (!selected) {
      selected = idle_task;
      DEBUG_printk("[sched] Selected 'idle'\n");
   }

   if (selected == current) {
      task_change_state(selected, TASK_STATE_RUNNING);
      selected->ticks = 0;
      return;
   }

   switch_to_task(selected);
}

// TODO: make this function much faster (e.g. indexing by pid)
task_info *get_task(int pid)
{
   task_info *pos;
   task_info *res = NULL;

   disable_preemption();

   list_for_each(pos, &tasks_list, list) {
      if (pos->pid == pid) {
         res = pos;
         break;
      }
   }

   enable_preemption();
   return res;
}

