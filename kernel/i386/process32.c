
#include <process.h>
#include <string_util.h>
#include <kmalloc.h>
#include <arch/i386/process_int.h>

static inline void push_on_user_stack(regs *r, uptr val)
{
   memcpy((void *)r->useresp, &val, sizeof(val));
   r->useresp -= sizeof(val);
}

NORETURN void first_usermode_switch(page_directory_t *pdir,
                                    void *entry,
                                    void *stack_addr)
{
   const char prog_name[4] = "ini\0";

   regs r;
   memset(&r, 0, sizeof(r));

   // User data selector with bottom 2 bits set for ring 3.
   r.gs = r.fs = r.es = r.ds = r.ss = 0x23;

   // User code selector with bottom 2 bits set for ring 3.
   r.cs = 0x1b;

   r.eip = (u32) entry;
   r.useresp = (u32) stack_addr;

   // push ini\0
   push_on_user_stack(&r, *((uptr*)prog_name));

   // env itself (1 entry containing NULL)
   push_on_user_stack(&r, 0);

   // push 0 ; env
   push_on_user_stack(&r, r.useresp - 4);

   // push the argv array
   push_on_user_stack(&r, 0);
   push_on_user_stack(&r, (uptr) stack_addr /* containing now prog_name */);

   // push argc
   push_on_user_stack(&r, 1);
   r.useresp += sizeof(uptr);


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
