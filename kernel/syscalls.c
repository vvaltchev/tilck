
#include <common_defs.h>
#include <string_util.h>
#include <term.h>
#include <irq.h>
#include <process.h>

typedef sptr (*syscall_type)();

sptr sys_setup()
{
   return 0;
}

sptr sys_exit(int code);

sptr sys_fork();

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

sptr sys_open(const char *pathname, int flags, int mode)
{
   printk("sys_open(filename = '%s', "
          "flags = %x, mode = %x)\n", pathname, flags, mode);
   return 825;
}

sptr sys_close(int fd)
{
   printk("sys_close(fd = %d)\n", fd);
   return 0;
}

sptr sys_waitpid()
{
   return 0;
}

sptr sys_creat()
{
   return 0;
}

sptr sys_link()
{
   return 0;
}

sptr sys_unlink()
{
   return 0;
}

sptr sys_execve()
{
   return 0;
}

sptr sys_chdir()
{
   return 0;
}

sptr sys_time()
{
   return 0;
}

sptr sys_mknod()
{
   return 0;
}

sptr sys_chmod()
{
   return 0;
}

sptr sys_lchown()
{
   return 0;
}

sptr sys_break()
{
   return 0;
}

sptr sys_oldstat()
{
   return 0;
}

sptr sys_lseek()
{
   return 0;
}

sptr sys_getpid();

#ifdef __i386__

// The syscall numbers are ARCH-dependent
syscall_type syscalls_pointers[] =
{
   sys_setup,    //  0
   sys_exit,     //  1
   sys_fork,     //  2
   sys_read,     //  3
   sys_write,    //  4
   sys_open,     //  5
   sys_close,    //  6
   sys_waitpid,  //  7
   sys_creat,    //  8
   sys_link,     //  9
   sys_unlink,   // 10
   sys_execve,   // 11
   sys_chdir,    // 12
   sys_time,     // 13
   sys_mknod,    // 14
   sys_chmod,    // 15
   sys_lchown,   // 16
   sys_break,    // 17
   sys_oldstat,  // 18
   sys_lseek,    // 19
   sys_getpid    // 20
};

const ssize_t syscall_count = ARRAY_SIZE(syscalls_pointers);


#include <arch/i386/arch_utils.h>

void handle_syscall(regs *r)
{
   save_current_process_state(r);

   sptr syscall_no = r->eax;

   if (syscall_no < 0 || syscall_no >= syscall_count) {
      printk("INVALID syscall #%i\n", syscall_no);
      return;
   }

   //printk("Syscall #%i\n", r->eax);
   //printk("Arg 1 (ebx): %p\n", r->ebx);
   //printk("Arg 2 (ecx): %p\n", r->ecx);
   //printk("Arg 3 (edx): %p\n", r->edx);
   //printk("Arg 4 (esi): %p\n", r->esi);
   //printk("Arg 5 (edi): %p\n", r->edi);
   //printk("Arg 6 (ebp): %p\n\n", r->ebp);

   r->eax =
      syscalls_pointers[r->eax](r->ebx, r->ecx, r->edx,
                                r->esi, r->edi, r->ebp);
}

#endif
