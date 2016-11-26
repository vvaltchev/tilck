
#include <process.h>
#include <stringUtil.h>

extern volatile uint32_t timer_ticks;

#ifdef __i386__

#include <arch/i386/arch_utils.h>

void schedule(regs *r)
{
   if ((timer_ticks % 500)) return;

   printk("sched!\n");
   printk("current pdir is %p\n", get_curr_page_dir());
   printk("eip: %p\n", r->eip);
}

#endif