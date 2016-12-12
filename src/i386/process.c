
#include <process.h>
#include <stringUtil.h>
#include <kmalloc.h>
#include <arch/i386/process_int.h>

extern volatile u32 timer_ticks;
void asm_context_switch_x86(u32 d, ...) NORETURN;

process_info *processes_list = NULL;
process_info *current_process = NULL;

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

void add_process(process_info *p)
{
   if (!processes_list) {
      p->next = p;
      p->prev = p;
      processes_list = p;
      return;
   }

   process_info *last = processes_list->prev;

   last->next = p;
   p->prev = last;
   p->next = processes_list;
   processes_list->prev = p;   
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

   process_info *pi = kmalloc(sizeof(process_info));
   pi->pdir = pdir;
   pi->pid = ++current_max_pid;
   memmove(&pi->state_regs, &r, sizeof(r));

   add_process(pi);

   current_process = pi;
   set_page_directory(pdir);
   context_switch(&pi->state_regs);
}

void save_current_process_state(regs *r)
{
   memmove(&current_process->state_regs, r, sizeof(*r));

   //printk("save_current_process_state(), eip = %p\n", r->eip);

   if (r->int_no >= 32) {
      PIC_sendEOI(r->int_no - 32);
   }
}

void switch_to_process(process_info *pi)
{
   current_process = pi;

   printk("[sched] Switching to pid: %i\n", current_process->pid);

   if (get_curr_page_dir() != current_process->pdir) {
      //printk("[kernel] Switch pdir to %p\n", current_process->pdir);
      set_page_directory(current_process->pdir);
   }

   //printk("context_switch() to eip = %p\n", current_process->state_regs.eip);
   context_switch(&current_process->state_regs);
}

// Returns child's pid
void fork_current_process()
{
   page_directory_t *pdir = pdir_clone(current_process->pdir);
   
   process_info *child = kmalloc(sizeof(process_info));
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

   switch_to_process(child);
}

void schedule()
{
   //printk("sched!\n");
   //printk("Current pid: %i\n", current_process->pid);

   //printk("current pdir is %p\n", get_curr_page_dir());
   //printk("eip: %p\n", r->eip);

   switch_to_process(current_process->next);
}

