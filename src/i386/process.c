
#include <process.h>
#include <stringUtil.h>
#include <arch/i386/process_int.h>

extern volatile u32 timer_ticks;


void schedule(regs *r)
{
   if ((timer_ticks % 500)) return;

   printk("sched!\n");
   printk("current pdir is %p\n", get_curr_page_dir());
   printk("eip: %p\n", r->eip);
}

