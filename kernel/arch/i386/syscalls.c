
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/irq.h>
#include <exos/process.h>
#include <exos/hal.h>
#include <exos/errno.h>
#include <exos/timer.h>
#include <exos/debug_utils.h>

typedef sptr (*syscall_type)();

// 1:
sptr sys_exit(int code);
sptr sys_fork(void);
sptr sys_read(int fd, void *buf, size_t count);
sptr sys_write(int fd, const void *buf, size_t count);
sptr sys_open(const char *pathname, int flags, int mode);
sptr sys_close(int fd);
sptr sys_waitpid(int pid, int *wstatus, int options);

sptr sys_creat()
{
   return -ENOSYS;
}

sptr sys_link()
{
   return -ENOSYS;
}

sptr sys_unlink()
{
   return -ENOSYS;
}

sptr sys_execve(const char *filename,
                const char *const *argv,
                const char *const *env);

sptr sys_chdir(const char *path);

sptr sys_time()
{
   return -ENOSYS;
}

sptr sys_mknod()
{
   return -ENOSYS;
}

sptr sys_chmod()
{
   return -ENOSYS;
}

sptr sys_lchown()
{
   return -ENOSYS;
}

sptr sys_break()
{
   return -ENOSYS;
}

sptr sys_oldstat()
{
   return -ENOSYS;
}

sptr sys_lseek()
{
   return -ENOSYS;
}

sptr sys_getpid();

sptr sys_mount()
{
   return -ENOSYS;
}

sptr sys_oldumount()
{
   return -ENOSYS;
}

/* Actual implementation: accept only 0 as UID. */
sptr sys_setuid16(uptr uid)
{
   if (uid == 0)
      return 0;

   return -EINVAL;
}

/* Actual implementation, not a stub: only the root user exists. */
sptr sys_getuid16()
{
   return 0;
}

sptr sys_stime()
{
   return -ENOSYS;
}

sptr sys_ptrace()
{
   return -ENOSYS;
}

sptr sys_alarm()
{
   return -ENOSYS;
}

sptr sys_oldfstat()
{
   return -ENOSYS;
}

sptr sys_pause();

sptr sys_utime()
{
   return -ENOSYS;
}

// 54:
sptr sys_ioctl(int fd, uptr request, void *argp);

// 146:
sptr sys_writev(int fd, const void *iov, int iovcnt);

// 162:
sptr sys_nanosleep(/* ignored arguments for the moment */)
{
   // This is a stub implementation. TODO: actually implement nanosleep().
   kernel_sleep(TIMER_HZ/10);
   return 0;
}

//183:
sptr sys_getcwd(char *buf, size_t buf_size);

//243:
sptr sys_set_thread_area(void *u_info);

//258:

// TODO: complete the implementation when thread creation is implemented.
sptr sys_set_tid_address(int *tidptr);

// The syscall numbers are ARCH-dependent
syscall_type syscalls[] =
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
   [22] = sys_oldumount,
   [23] = sys_setuid16,
   [24] = sys_getuid16,
   [25] = sys_stime,
   [26] = sys_ptrace,
   [27] = sys_alarm,
   [28] = sys_oldfstat,
   [29] = sys_pause,
   [30] = sys_utime,
   [54] = sys_ioctl,

   [146] = sys_writev,
   [162] = sys_nanosleep,
   [183] = sys_getcwd,
   [243] = sys_set_thread_area,
   [258] = sys_set_tid_address
};

void handle_syscall(regs *r)
{
   ASSERT(get_curr_task() != NULL);
   DEBUG_VALIDATE_STACK_PTR();

   /*
    * In case of a sysenter syscall, the eflags are saved in kernel mode after
    * the cpu disabled the interrupts. Therefore, with the statement below we
    * force the IF flag to be set in any case (for the int 0x80 case it is not
    * necessary).
    */
   r->eflags |= EFLAGS_IF;
   save_current_task_state(r);

   const u32 sn = r->eax;

   if (sn >= ARRAY_SIZE(syscalls) || !syscalls[sn]) {
      printk("invalid syscall #%i\n", sn);
      r->eax = (uptr) -ENOSYS;
      return;
   }

   set_current_task_in_kernel();
   DEBUG_VALIDATE_STACK_PTR();
   enable_preemption();
   {
      r->eax = syscalls[sn](r->ebx, r->ecx, r->edx, r->esi, r->edi, r->ebp);
   }
   disable_preemption();
   DEBUG_VALIDATE_STACK_PTR();
   set_current_task_in_user_mode();
}

#include "idt_int.h"

void syscall_int80_entry(void);
void sysenter_entry(void);
void asm_sysenter_setup(void);

void setup_syscall_interfaces(void)
{
   /* Set the entry for the int 0x80 syscall interface */
   idt_set_entry(0x80,
                 syscall_int80_entry,
                 X86_KERNEL_CODE_SEL,
                 IDT_FLAG_PRESENT | IDT_FLAG_INT_GATE | IDT_FLAG_DPL3);

   /* Setup the sysenter interface */
   wrmsr(MSR_IA32_SYSENTER_CS, X86_KERNEL_CODE_SEL);
   wrmsr(MSR_IA32_SYSENTER_EIP, (uptr) &sysenter_entry);

   asm_sysenter_setup();
}

