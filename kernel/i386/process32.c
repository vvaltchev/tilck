
#include <process.h>
#include <string_util.h>
#include <kmalloc.h>
#include <arch/i386/process_int.h>

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
