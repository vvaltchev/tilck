
#include <process.h>
#include <stringUtil.h>
#include <kmalloc.h>
#include <arch/i386/process_int.h>

extern volatile u32 timer_ticks;
void asm_context_switch_x86(u32 d, ...) NORETURN;

process_info *processes = NULL;

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
   if (!processes) {
      processes = p;
      return;
   }

   // TODO: finish the implementation.
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
   pi->next = NULL;
   pi->state_regs = r;
   pi->pdir = pdir;

   add_process(pi);

   set_page_directory(pdir);
   context_switch(&processes->state_regs);
}


void schedule(regs *r)
{
   if ((timer_ticks % 500)) return;

   printk("sched!\n");
   printk("current pdir is %p\n", get_curr_page_dir());
   printk("eip: %p\n", r->eip);

   if (r->int_no >= 32) {
      /* 
       * We are here because of an IRQ, likely the timer.
       * and we have to send EOI to the PIC otherwise
       * we won't get anymore IRQs.
       */
      PIC_sendEOI(r->int_no - 32);
   }

   context_switch(r);
}

