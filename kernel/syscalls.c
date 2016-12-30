
#include <common_defs.h>
#include <string_util.h>
#include <term.h>
#include <irq.h>
#include <process.h>

typedef sptr (*syscall_type)();

// 0
sptr sys_setup()
{
   return 0;
}

// 1
sptr sys_exit(int code);

// 2
sptr sys_fork();

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

// 8
sptr sys_creat()
{
   return 0;
}

// 9
sptr sys_link()
{
   return 0;
}

// 10
sptr sys_unlink()
{
   return 0;
}

// 11
sptr sys_execve()
{
   return 0;
}

// 12
sptr sys_chdir()
{
   return 0;
}

// 13
sptr sys_time()
{
   return 0;
}

// 14
sptr sys_mknod()
{
   return 0;
}

// 15
sptr sys_chmod()
{
   return 0;
}

// 16
sptr sys_lchown()
{
   return 0;
}

// 17
sptr sys_break()
{
   return 0;
}

// 18
sptr sys_oldstat()
{
   return 0;
}

// 19
sptr sys_lseek()
{
   return 0;
}

// 20
sptr sys_getpid();

syscall_type syscalls_pointers[] =
{
   sys_setup,
   sys_exit,
   sys_fork,
   sys_read,
   sys_write,
   sys_open,
   sys_close,
   sys_waitpid,
   sys_creat,
   sys_link,
   sys_unlink,
   sys_execve,
   sys_chdir,
   sys_time,
   sys_mknod,
   sys_chmod,
   sys_lchown,
   sys_break,
   sys_oldstat,
   sys_lseek,
   sys_getpid
};

const ssize_t syscall_count = ARRAY_SIZE(syscalls_pointers);

#ifdef __i386__

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
