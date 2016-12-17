
#include <commonDefs.h>
#include <string_util.h>
#include <term.h>
#include <irq.h>
#include <process.h>

typedef sptr (*syscall_type)();

// 0
sptr sys_restart_syscall()
{
   return 0;
}

// 1
sptr sys_exit()
{
   return 0;
}

// 2
sptr sys_fork()
{
   fork_current_process();
   return 0;
}

// 3
sptr sys_read(int fd, void *buf, size_t count)
{
   //printk("sys_read(fd = %i, count = %u)\n", fd, count);
   return 0;
}

sptr sys_write(int fd, const void *buf, size_t count)
{
   //printk("sys_write(fd = %i, count = %u)\n", fd, count);

   for (size_t i = 0; i < count; i++) {
      term_write_char(((char *)buf)[i]);
   }

   return 0;
}

// 5
sptr sys_open(const char *pathname, int flags, int mode)
{
   printk("sys_open(filename = '%s', "
          "flags = %x, mode = %x)\n", pathname, flags, mode);
   return 825;
}

// 6
sptr sys_close(int fd)
{
   printk("sys_close(fd = %d)\n", fd);
   return 0;
}

// 7
sptr sys_waitpid()
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

ssize_t syscall_count = sizeof(syscalls_pointers) / sizeof(void *);

#ifdef __i386__

#include <arch/i386/arch_utils.h>

sptr handle_syscall(regs *r)
{
   sptr syscall_no = r->eax;

   if (syscall_no < 0 || syscall_no >= syscall_count) {
      printk("INVALID syscall #%i\n", syscall_no);
      return -1;
   }

   //printk("Syscall #%i\n", r->eax);
   //printk("Arg 1 (ebx): %p\n", r->ebx);
   //printk("Arg 2 (ecx): %p\n", r->ecx);
   //printk("Arg 3 (edx): %p\n", r->edx);
   //printk("Arg 4 (esi): %p\n", r->esi);
   //printk("Arg 5 (edi): %p\n", r->edi);
   //printk("Arg 6 (ebp): %p\n\n", r->ebp);

   if (syscall_no == 2 /* fork */) {
      printk("syscall is fork(), saving state..\n");
      save_current_process_state(r);
   }

   r->eax =
      syscalls_pointers[r->eax](r->ebx, r->ecx, r->edx,
                                r->esi, r->edi, r->ebp);

   return 0;
}

#endif
