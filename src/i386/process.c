
#include <process.h>
#include <string_util.h>
#include <kmalloc.h>
#include <arch/i386/process_int.h>

extern volatile u32 timer_ticks;
void asm_context_switch_x86(u32 d, ...) NORETURN;

task_info *processes_list = NULL;
task_info *current_process = NULL;

int current_max_pid = -1;

static ALWAYS_INLINE void context_switch(regs *r)
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

   if (!processes_list) {
      p->next = p;
      p->prev = p;
      processes_list = p;
      return;
   }

   task_info *last = processes_list->prev;

   last->next = p;
   p->prev = last;
   p->next = processes_list;
   processes_list->prev = p;   
}

void remove_process(task_info *p)
{
   p->prev->next = p->next;
   p->next->prev = p->prev;

   printk("[remove_process] pid = %i\n", p->pid);
}

void exit_current_process(int exit_code)
{
   printk("[kernel] Exit process %i with code = %i\n",
          current_process->pid,
          exit_code);

   current_process->state = TASK_STATE_ZOMBIE;
   current_process->exit_code = exit_code;
   pdir_destroy(current_process->pdir);
   schedule();
}

void first_usermode_switch(page_directory_t *pdir,
                           void *entry, void *stack_addr)
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
   pi->pdir = pdir;
   pi->pid = ++current_max_pid;
   pi->state = TASK_STATE_RUNNABLE;
   memmove(&pi->state_regs, &r, sizeof(r));

   add_process(pi);

   current_process = pi;
   set_page_directory(pdir);
   context_switch(&pi->state_regs);
}

void save_current_process_state(regs *r)
{
   memmove(&current_process->state_regs, r, sizeof(*r));
}

extern volatile int current_interrupt_num;

void switch_to_process(task_info *pi)
{
   ASSERT(pi->state == TASK_STATE_RUNNABLE);

   current_process = pi;
   current_process->state = TASK_STATE_RUNNING;

   printk("[sched] Switching to pid: %i\n", current_process->pid);

   if (get_curr_page_dir() != current_process->pdir) {
      set_page_directory(current_process->pdir);
   }

   if (current_interrupt_num >= 32 && current_interrupt_num != 0x80) {
      //printk("[sched] PIC_sendEOI for IRQ #%i\n", current_interrupt_num - 32);
      PIC_sendEOI(current_interrupt_num - 32);
   }

   context_switch(&current_process->state_regs);
}

// Returns child's pid
int fork_current_process()
{
   page_directory_t *pdir = pdir_clone(current_process->pdir);
   
   task_info *child = kmalloc(sizeof(task_info));
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

void schedule()
{
   printk("[sched] Current pid: %i\n", current_process->pid);

   task_info *curr = current_process;
   task_info *p = curr;

   if (curr->state == TASK_STATE_RUNNING) {
      curr->state = TASK_STATE_RUNNABLE;
   }

   do {
      p = p->next;

      if (p->state == TASK_STATE_RUNNABLE) {
         break;
      }

   } while (p != curr);  

   if (p->state != TASK_STATE_RUNNABLE) {

      printk("[sched] No runnable process found. Halt.\n");

      if (current_interrupt_num >= 32) {
         PIC_sendEOI(current_interrupt_num - 32);
      }

      // We did not found any runnable task. Halt.
      halt();
   }

   switch_to_process(p);
}

