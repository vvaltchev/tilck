
#include <common_defs.h>
#include <string_util.h>
#include <term.h>
#include <irq.h>
#include <process.h>
#include <hal.h>
#include <errno.h>

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

   disable_preemption();

   for (size_t i = 0; i < count; i++) {
      term_write_char(((char *)buf)[i]);
   }

   enable_preemption();
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

sptr sys_mount()
{
   return 0;
}

sptr sys_umount()
{
   return 0;
}

sptr sys_setuid()
{
   return 0;
}

/* Actual implementation, not a stub: only the root user exists. */
sptr sys_getuid()
{
   return 0;
}

sptr sys_stime()
{
   return 0;
}

sptr sys_ptrace()
{
   return 0;
}

sptr sys_alarm()
{
   return 0;
}

sptr sys_oldfstat()
{
   return 0;
}

sptr sys_pause();

sptr sys_utime()
{
   return 0;
}

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
   [20] = sys_getpid,
   [21] = sys_mount,
   [22] = sys_umount,
   [23] = sys_setuid,
   [24] = sys_getuid,
   [25] = sys_stime,
   [26] = sys_ptrace,
   [27] = sys_alarm,
   [28] = sys_oldfstat,
   [29] = sys_pause,
   [30] = sys_utime
};

const ssize_t syscall_count = ARRAY_SIZE(syscalls_pointers);


#include <hal.h>

void handle_syscall(regs *r)
{
   save_current_task_state(r);

   sptr syscall_no = (sptr) r->eax;

   if (syscall_no < 0 || syscall_no >= syscall_count) {
      printk("INVALID syscall #%i\n", syscall_no);
      r->eax = (uptr) -ENOSYS;
      return;
   }

   //printk("Syscall #%i\n", r->eax);
   //printk("Arg 1 (ebx): %p\n", r->ebx);
   //printk("Arg 2 (ecx): %p\n", r->ecx);
   //printk("Arg 3 (edx): %p\n", r->edx);
   //printk("Arg 4 (esi): %p\n", r->esi);
   //printk("Arg 5 (edi): %p\n", r->edi);
   //printk("Arg 6 (ebp): %p\n\n", r->ebp);

   set_current_task_in_kernel();
   enable_preemption();

   r->eax =
      syscalls_pointers[r->eax](r->ebx, r->ecx, r->edx,
                                r->esi, r->edi, r->ebp);

   disable_preemption();
   set_current_task_in_user_mode();
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
