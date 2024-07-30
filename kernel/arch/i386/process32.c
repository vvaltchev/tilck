/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_mm.h>
#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>
#include <tilck/common/unaligned.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/worker_thread.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/signal.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/vdso.h>
#include <tilck/kernel/switch.h>

#include <tilck/mods/tracing.h>

#include "gdt_int.h"

void soft_interrupt_resume(void);

//#define DEBUG_printk printk
#define DEBUG_printk(...)

STATIC_ASSERT(
   OFFSET_OF(struct task, fault_resume_regs) == TI_F_RESUME_RS_OFF
);
STATIC_ASSERT(
   OFFSET_OF(struct task, faults_resume_mask) == TI_FAULTS_MASK_OFF
);

STATIC_ASSERT(sizeof(struct task_and_process) <= 1024);

int setup_sig_handler(struct task *ti,
                      enum sig_state sig_state,
                      regs_t *r,
                      ulong user_func,
                      int signum)
{
   if (ti->nested_sig_handlers == 0) {

      int rc;

      if (sig_state == sig_pre_syscall)
         r->eax = (ulong) -EINTR;

      if ((rc = save_regs_on_user_stack(r)) < 0)
         return rc;
   }

   r->eip = user_func;
   r->useresp -= SIG_HANDLER_ALIGN_ADJUST;
   push_on_user_stack(r, (ulong)signum);
   push_on_user_stack(r, post_sig_handler_user_vaddr);
   ti->nested_sig_handlers++;

   /*
    * Check that the stack pointer + 4 is aligned at a 16-bytes boundary.
    * The reason for that +4 (word size) is that the stack must be aligned
    * BEFORE the call instruction, not after it. So, at the first instruction,
    * the callee will see its ESP in hex ending with a "c", like this:
    *
    *    0xbfffce2c             # if we add +4, it's aligned at 16
    *
    * and NOT like this:
    *
    *    0xbfffce20             # it's already aligned at 16
    */
   ASSERT(((r->useresp + sizeof(ulong)) & (USERMODE_STACK_ALIGN - 1)) == 0);
   return 0;
}

void
kthread_create_init_regs_arch(regs_t *r, void *func)
{
   *r = (regs_t) {
      .kernel_resume_eip = (ulong)&soft_interrupt_resume,
      .custom_flags = 0,
      .gs = X86_KERNEL_DATA_SEL,
      .fs = X86_KERNEL_DATA_SEL,
      .es = X86_KERNEL_DATA_SEL,
      .ds = X86_KERNEL_DATA_SEL,
      .edi = 0, .esi = 0, .ebp = 0, .esp = 0,
      .ebx = 0, .edx = 0, .ecx = 0, .eax = 0,
      .int_num = 0,
      .err_code = 0,
      .eip = (ulong)func,
      .cs = X86_KERNEL_CODE_SEL,
      .eflags = 0x2 /* reserved, should be always set */ | EFLAGS_IF,
      .useresp = 0,
      .ss = X86_KERNEL_DATA_SEL,
   };
}

void
kthread_create_setup_initial_stack(struct task *ti, regs_t *r, void *arg)
{
   /*
    * 1) Push into the stack, function's argument, `arg`.
    *
    * 2) Push the address of kthread_exit() into thread's stack in order to it
    *    to be called after thread's function returns. It's AS IF kthread_exit
    *    called the thread `func` with a CALL instruction before doing anything
    *    else. That allows the RET by `func` to jump at the begging of
    *    kthread_exit().
    *
    * 3) Reserve space for the regs on the stack
    * 4) Copy the actual regs to the new stack
    */

   push_on_stack((ulong **)&ti->state_regs, (ulong)arg);
   push_on_stack((ulong **)&ti->state_regs, (ulong)&kthread_exit);
   ti->state_regs = (void *)ti->state_regs - sizeof(regs_t) + 8;
   memcpy(ti->state_regs, r, sizeof(*r) - 8);
}

void
setup_usermode_task_regs(regs_t *r, void *entry, void *stack_addr)
{
   *r = (regs_t) {
      .kernel_resume_eip = (ulong)&soft_interrupt_resume,
      .custom_flags = 0,
      .gs = X86_USER_DATA_SEL,
      .fs = X86_USER_DATA_SEL,
      .es = X86_USER_DATA_SEL,
      .ds = X86_USER_DATA_SEL,
      .edi = 0, .esi = 0, .ebp = 0, .esp = 0,
      .ebx = 0, .edx = 0, .ecx = 0, .eax = 0,
      .int_num = 0,
      .err_code = 0,
      .eip = (ulong)entry,
      .cs = X86_USER_CODE_SEL,
      .eflags = 0x2 /* reserved, should be always set */ | EFLAGS_IF,
      .useresp = (ulong)stack_addr,
      .ss = X86_USER_DATA_SEL,
   };
}

/*
 * Sched functions that are here because of arch-specific statements.
 */

static inline bool
is_fpu_enabled_for_task(struct task *ti)
{
   return get_task_arch_fields(ti)->fpu_regs &&
          (ti->state_regs->custom_flags & REGS_FL_FPU_ENABLED);
}

static inline void
save_curr_fpu_ctx_if_enabled(void)
{
   if (is_fpu_enabled_for_task(get_curr_task())) {
      hw_fpu_enable();
      save_current_fpu_regs(false);
      hw_fpu_disable();
   }
}

NORETURN void
switch_to_task(struct task *ti)
{
   /* Save the value of ti->state_regs as it will be reset below */
   regs_t *state = ti->state_regs;
   struct task *curr = get_curr_task();

   ASSERT(curr != NULL);

   if (UNLIKELY(ti != curr)) {
      ASSERT(curr->state != TASK_STATE_RUNNING);
      ASSERT_TASK_STATE(ti->state, TASK_STATE_RUNNABLE);
   }

   ASSERT(!is_preemption_enabled());
   switch_to_task_safety_checks(curr, ti);

   /* Do as much as possible work before disabling the interrupts */
   task_change_state_idempotent(ti, TASK_STATE_RUNNING);
   ti->ticks.timeslice = 0;

   if (!is_kernel_thread(curr) && curr->state != TASK_STATE_ZOMBIE)
      save_curr_fpu_ctx_if_enabled();

   if (!is_kernel_thread(ti)) {

      if (get_curr_pdir() != ti->pi->pdir) {

         arch_proc_members_t *arch = get_proc_arch_fields(ti->pi);
         set_curr_pdir(ti->pi->pdir);

         if (UNLIKELY(arch->ldt != NULL))
            load_ldt(arch->ldt_index_in_gdt, arch->ldt_size);
      }

      if (!ti->running_in_kernel)
         process_signals(ti, sig_in_usermode, state);

      if (is_fpu_enabled_for_task(ti)) {
         hw_fpu_enable();
         restore_fpu_regs(ti, false);
         /* leave FPU enabled */
      }
   }

   /* From here until the end, we have to be as fast as possible */
   disable_interrupts_forced();
   switch_to_task_pop_nested_interrupts();
   enable_preemption_nosched();
   ASSERT(is_preemption_enabled());

   if (!running_in_kernel(ti))
      task_info_reset_kernel_stack(ti);
   else if (in_syscall(ti))
      adjust_nested_interrupts_for_task_in_kernel(ti);

   set_curr_task(ti);
   ti->timer_ready = false;
   set_kernel_stack((ulong)ti->state_regs);
   context_switch(state);
}

void
arch_specific_new_proc_setup(struct process *pi, struct process *parent)
{
   arch_proc_members_t *arch = get_proc_arch_fields(pi);

   if (!parent)
      return;      /* we're done */

   memcpy(&pi->pi_arch, &parent->pi_arch, sizeof(pi->pi_arch));

   if (arch->ldt)
      gdt_entry_inc_ref_count(arch->ldt_index_in_gdt);

   for (int i = 0; i < ARRAY_SIZE(arch->gdt_entries); i++)
      if (arch->gdt_entries[i])
         gdt_entry_inc_ref_count(arch->gdt_entries[i]);

   pi->set_child_tid = NULL;
}

void
arch_specific_free_proc(struct process *pi)
{
   arch_proc_members_t *arch = get_proc_arch_fields(pi);

   if (arch->ldt) {
      gdt_clear_entry(arch->ldt_index_in_gdt);
      arch->ldt = NULL;
   }

   for (int i = 0; i < ARRAY_SIZE(arch->gdt_entries); i++) {
      if (arch->gdt_entries[i]) {
         gdt_clear_entry(arch->gdt_entries[i]);
         arch->gdt_entries[i] = 0;
      }
   }
}

static void
handle_fatal_error(regs_t *r, int signum)
{
   send_signal(get_curr_tid(), signum, SIG_FL_PROCESS | SIG_FL_FAULT);
}

/* General protection fault handler */
void handle_gpf(regs_t *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("General protection fault. Error: %p\n", r->err_code);

   handle_fatal_error(r, SIGSEGV);
}

/* Illegal instruction fault handler */
void handle_ill(regs_t *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("Illegal instruction fault. Error: %p\n", r->err_code);

   handle_fatal_error(r, SIGILL);
}

/* Division by zero fault handler */
void handle_div0(regs_t *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("Division by zero fault. Error: %p\n", r->err_code);

   handle_fatal_error(r, SIGFPE);
}

/* Coproc fault handler */
void handle_cpf(regs_t *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("Co-processor (fpu) fault. Error: %p\n", r->err_code);

   handle_fatal_error(r, SIGFPE);
}
