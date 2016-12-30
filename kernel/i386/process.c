
#include <process.h>
#include <string_util.h>
#include <kmalloc.h>
#include <arch/i386/process_int.h>

extern volatile u64 jiffies;
NORETURN void asm_context_switch_x86(u32 d, ...);

task_info *current_process = NULL;

// Our linked list for all the tasks (processes, threads, etc.)
LIST_HEAD(tasks_list);

int current_max_pid = 0;

NORETURN static ALWAYS_INLINE void context_switch(regs *r)
{
   asm_context_switch_x86(
                          // Segment registers
                          r->gs,
                          r->fs,
                          r->es,
                          r->ds,

                          // General purpose registers
                          r->edi,
                          r->esi,
                          r->ebp,
                          /* skipping ESP */
                          r->ebx,
                          r->edx,
                          r->ecx,
                          r->eax,

                          // Registers pop-ed by iret
                          r->eip,
                          r->cs,
                          r->eflags,
                          r->useresp,
                          r->ss);
}

void add_process(task_info *p)
{
   p->state = TASK_STATE_RUNNABLE;
   list_add_tail(&tasks_list, &p->list);
}

void remove_process(task_info *p)
{
   list_remove(&p->list);
   printk("[remove_process] pid = %i\n", p->pid);
}


void save_current_process_state(regs *r)
{
   memmove(&current_process->state_regs, r, sizeof(*r));
}

extern volatile int current_interrupt_num;

#define TIME_SLOT_JIFFIES 250

NORETURN static void switch_to_process(task_info *pi)
{
   ASSERT(pi->state == TASK_STATE_RUNNABLE);

   current_process = pi;
   current_process->state = TASK_STATE_RUNNING;

   //printk("[sched] Switching to pid: %i\n", current_process->pid);

   if (get_curr_page_dir() != current_process->pdir) {
      set_page_directory(current_process->pdir);
   }

   if (current_interrupt_num >= 32 && current_interrupt_num != 0x80) {
      PIC_sendEOI(current_interrupt_num - 32);
   }

   context_switch(&current_process->state_regs);
}


NORETURN void first_usermode_switch(page_directory_t *pdir,
                                    void *entry,
                                    void *stack_addr)
{
   regs r;
   memset(&r, 0, sizeof(r));

   // User data selector with bottom 2 bits set for ring 3.
   r.gs = r.fs = r.es = r.ds = r.ss = 0x23;

   // User code selector with bottom 2 bits set for ring 3.
   r.cs = 0x1b;

   r.eip = (u32) entry;
   r.useresp = (u32) stack_addr;

   asmVolatile("pushf");
   asmVolatile("pop %eax");
   asmVolatile("movl %0, %%eax" : "=r"(r.eflags));

   task_info *pi = kmalloc(sizeof(task_info));
   INIT_LIST_HEAD(&pi->list);
   pi->pdir = pdir;
   pi->pid = ++current_max_pid;
   pi->state = TASK_STATE_RUNNABLE;
   memmove(&pi->state_regs, &r, sizeof(r));

   add_process(pi);
   pi->jiffies_when_switch = jiffies;
   switch_to_process(pi);
}

NORETURN void schedule()
{
   task_info *const curr = current_process;
   task_info *selected = curr;
   const u64 jiffies_used = jiffies - curr->jiffies_when_switch;

   if (jiffies_used < TIME_SLOT_JIFFIES && curr->state == TASK_STATE_RUNNING) {
      curr->state = TASK_STATE_RUNNABLE;
      goto end;
   }

   printk("[sched] Current pid: %i\n", current_process->pid);
   printk("[sched] Used %llu jiffies\n", jiffies_used);

   // If we preempted the process, it is still runnable.
   if (curr->state == TASK_STATE_RUNNING) {
      curr->state = TASK_STATE_RUNNABLE;
   }

   // Actual scheduling logic.

   task_info *pos;
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

      if (current_interrupt_num >= 32 && current_interrupt_num != 0x80) {
         PIC_sendEOI(current_interrupt_num - 32);
      }

      // We did not found any runnable task. Halt.
      halt();
   }

   if (selected != curr) {
      printk("[sched] Switching to pid: %i\n", selected->pid);
   }

   switch_to_process(selected);
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
   ASSERT(current_process != NULL);
   return current_process->pid;
}

NORETURN void sys_exit(int exit_code)
{
   printk("[kernel] Exit process %i with code = %i\n",
          current_process->pid,
          exit_code);

   current_process->state = TASK_STATE_ZOMBIE;
   current_process->exit_code = exit_code;
   pdir_destroy(current_process->pdir);
   schedule();
}

// Returns child's pid
int sys_fork()
{
   page_directory_t *pdir = pdir_clone(current_process->pdir);

   task_info *child = kmalloc(sizeof(task_info));
   INIT_LIST_HEAD(&child->list);
   child->pdir = pdir;
   child->pid = ++current_max_pid;
   memmove(&child->state_regs,
           &current_process->state_regs,
           sizeof(child->state_regs));
   child->state_regs.eax = 0;

   //printk("forking current proccess with eip = %p\n", child->state_regs.eip);

   add_process(child);

   // Make the parent to get child's pid as return value.
   current_process->state_regs.eax = child->pid;

   /*
    * Force the CR3 reflush using the current (parent's) pdir.
    * Without doing that, COW on parent's pages doesn't work immediately.
    * That is better (in this case) than invalidating all the pages affected,
    * one by one.
    */

   set_page_directory(current_process->pdir);
   return child->pid;
}
