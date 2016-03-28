
#include <commonDefs.h>
#include <stringUtil.h>
#include <term.h>
#include <irq.h>

void handle_syscall_asm();


void handle_syscall(struct regs *r)
{
   printk("Syscall #%i\n", r->eax);
   printk("Arg 1 (ebx): %p\n", r->ebx);
   printk("Arg 2 (ecx): %p\n", r->ecx);
   printk("Arg 3 (edx): %p\n", r->edx);
   printk("Arg 4 (esi): %p\n", r->esi);
   printk("Arg 5 (edi): %p\n", r->edi);
   printk("Arg 6 (ebp): %p\n", r->ebp);
}


