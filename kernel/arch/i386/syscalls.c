
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/syscalls.h>
#include <exos/irq.h>
#include <exos/process.h>
#include <exos/hal.h>
#include <exos/errno.h>
#include <exos/timer.h>
#include <exos/debug_utils.h>

typedef sptr (*syscall_type)();

#define CREATE_STUB_IMPL(name)                          \
   sptr name()                                          \
   {                                                    \
      printk("[TID: %d] NOT IMPLEMENTED: %s()\n",       \
             get_curr_task()->tid, #name);              \
      return -ENOSYS;                                   \
   }

CREATE_STUB_IMPL(sys_creat)
CREATE_STUB_IMPL(sys_link)
CREATE_STUB_IMPL(sys_unlink)
CREATE_STUB_IMPL(sys_time)
CREATE_STUB_IMPL(sys_mknod)
CREATE_STUB_IMPL(sys_chmod)
CREATE_STUB_IMPL(sys_lchown)
CREATE_STUB_IMPL(sys_break)
CREATE_STUB_IMPL(sys_oldstat)
CREATE_STUB_IMPL(sys_lseek)
CREATE_STUB_IMPL(sys_mount)
CREATE_STUB_IMPL(sys_oldumount)
CREATE_STUB_IMPL(sys_stime)
CREATE_STUB_IMPL(sys_ptrace)
CREATE_STUB_IMPL(sys_alarm)
CREATE_STUB_IMPL(sys_oldfstat)
CREATE_STUB_IMPL(sys_utime)

sptr sys_rt_sigprocmask(/* args ignored at the moment */)
{
   // TODO: implement sys_rt_sigprocmask
   // printk("rt_sigprocmask\n");
   return 0;
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

sptr sys_nanosleep(/* ignored arguments for the moment */)
{
   // This is a stub implementation. TODO: actually implement nanosleep().
   kernel_sleep(TIMER_HZ/10);
   return 0;
}

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
   [114] = sys_wait4,
   [146] = sys_writev,
   [162] = sys_nanosleep,
   [175] = sys_rt_sigprocmask,
   [183] = sys_getcwd,
   [224] = sys_gettid,
   [243] = sys_set_thread_area,
   [252] = sys_exit_group,
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
      printk("Unknown syscall #%i\n", sn);
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

