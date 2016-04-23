
#include <commonDefs.h>
#include <stringUtil.h>
#include <term.h>
#include <irq.h>

typedef int32_t (*syscall_type)();

// 0
int32_t sys_restart_syscall()
{
   return 0;
}

// 1
int32_t sys_exit()
{
   return 0;
}

// 2
int32_t sys_fork()
{
   return 0;
}

// 3
int32_t sys_read()
{
   return 0;
}

// 4
int32_t sys_write(int fd, const void *buf, size_t count)
{
   printk("sys_write(fd = %i, buf = '%s', count = %u)\n", fd, buf, count);
   return 0;
}

// 5
int32_t sys_open(const char *pathname, int32_t flags, int32_t mode)
{
   printk("sys_open(filename = '%s', flags = %x, mode = %x)\n", pathname, flags, mode);
   return 3;
}

// 6
int32_t sys_close()
{
   return 0;
}

// 7
int32_t sys_waitpid()
{
   return 0;
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

int handle_syscall(struct regs *r)
{
   int syscall_no = r->eax;

   if (syscall_no < 0 || syscall_no >= syscall_count) {
      printk("INVALID syscall #%i\n", syscall_no);
      return -1;
   }

   printk("Syscall #%i\n", r->eax);
   //printk("Arg 1 (ebx): %p\n", r->ebx);
   //printk("Arg 2 (ecx): %p\n", r->ecx);
   //printk("Arg 3 (edx): %p\n", r->edx);
   //printk("Arg 4 (esi): %p\n", r->esi);
   //printk("Arg 5 (edi): %p\n", r->edi);
   //printk("Arg 6 (ebp): %p\n\n", r->ebp);

   void *sysCallPtr = syscalls_pointers[r->eax];

   int ret;

   asmVolatile(" \
     push %1; \
     push %2; \
     push %3; \
     push %4; \
     push %5; \
     call *%6; \
     addl $20, %%esp; \
   " : "=a" (ret)
     : "r" (r->edi),
       "r" (r->esi),
       "r" (r->edx),
       "r" (r->ecx),
       "r" (r->ebx),
       "r" (sysCallPtr));

   r->eax = ret;

   return 0;
}


