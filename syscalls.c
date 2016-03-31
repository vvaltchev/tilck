
#include <commonDefs.h>
#include <stringUtil.h>
#include <term.h>
#include <irq.h>

typedef void (*syscall_type)(struct regs *);

// 0
void sys_restart_syscall()
{

}

// 1
void sys_exit()
{

}

// 2
void sys_fork()
{

}

// 3
void sys_read()
{

}

// 4
void sys_write()
{

}

// 5
void sys_open(struct regs *r)
{
   printk("sys_open('%s', %x, %x)\n", r->ebx, r->ecx, r->edx);
}

// 6
void sys_close()
{

}

// 7
void sys_waitpid()
{

}

syscall_type syscalls_pointers[] =
{
   sys_restart_syscall,
   sys_exit,
   sys_fork,
   sys_read,
   sys_write,
   sys_open,
   sys_close,
   sys_waitpid
};

const int syscall_count = sizeof(syscalls_pointers) / sizeof(void *);

void handle_syscall(struct regs *r)
{
   int syscall_no = r->eax;

   if (syscall_no < 0 || syscall_no >= syscall_count) {
      printk("INVALID syscall #%i\n", syscall_no);
      return;
   }

   printk("Syscall #%i\n", r->eax);
   printk("Arg 1 (ebx): %p\n", r->ebx);
   printk("Arg 2 (ecx): %p\n", r->ecx);
   printk("Arg 3 (edx): %p\n", r->edx);
   printk("Arg 4 (esi): %p\n", r->esi);
   printk("Arg 5 (edi): %p\n", r->edi);
   printk("Arg 6 (ebp): %p\n\n", r->ebp);

   syscalls_pointers[r->eax](r);
}


