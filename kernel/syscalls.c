
#include <common_defs.h>
#include <string_util.h>
#include <term.h>
#include <irq.h>
#include <process.h>
#include <hal.h>

typedef sptr (*syscall_type)();

sptr sys_exit(int code);

sptr sys_fork();

sptr sys_read(int fd, void *buf, size_t count)
{
   //printk("sys_read(fd = %i, count = %u)\n", fd, count);
   return 0;
}

sptr sys_write(int fd, const void *buf, size_t count)
{
   //printk("sys_write(fd = %i, count = %u, buf = '%s')\n", fd, count, buf);

   //for (int i = 0; i < 50*1000*1000; i++) { } // waste some cylces

   for (size_t i = 0; i < count; i++) {
      term_write_char(((char *)buf)[i]);
   }

   return count;
}

sptr sys_open(const char *pathname, int flags, int mode)
{
   printk("[kernel] sys_open(filename = '%s', "
          "flags = %x, mode = %x)\n", pathname, flags, mode);
   return 825;
}

sptr sys_close(int fd)
{
   printk("[kernel] sys_close(fd = %d)\n", fd);
   return 0;
}

sptr sys_waitpid(int pid, int *wstatus, int options);

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
   [0] = NULL,
   [1] = sys_exit,
   [2] = sys_fork,
   [3] = sys_read,
   [4] = sys_write,
   [5] = sys_open,
   [6] = sys_close,
   [7] = sys_waitpid,
   [8] = sys_creat,
   [9] = sys_link,
   [10] = sys_unlink,
   [11] = sys_execve,
   [12] = sys_chdir,
   [13] = sys_time,
   [14] = sys_mknod,
   [15] = sys_chmod,
   [16] = sys_lchown,
   [17] = sys_break,
   [18] = sys_oldstat,
   [19] = sys_lseek,
   [20] = sys_getpid
};

const ssize_t syscall_count = ARRAY_SIZE(syscalls_pointers);


#include <hal.h>

void handle_syscall(regs *r)
{
   save_current_task_state(r);

   sptr syscall_no = (sptr) r->eax;

   if (syscall_no < 0 || syscall_no >= syscall_count) {
      printk("INVALID syscall #%i\n", syscall_no);
      r->eax = (uptr) -1;
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

#define MSR_IA32_SYSENTER_CS            0x174
#define MSR_IA32_SYSENTER_ESP           0x175
#define MSR_IA32_SYSENTER_EIP           0x176

void isr128();

void setup_sysenter_interface()
{
   wrmsr(MSR_IA32_SYSENTER_CS, 0x08 + 3);
   wrmsr(MSR_IA32_SYSENTER_ESP, get_kernel_stack());
   wrmsr(MSR_IA32_SYSENTER_EIP, (uptr) &isr128);
}

#endif
