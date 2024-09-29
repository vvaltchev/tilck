/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_mm.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/switch.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/vdso.h>
#include <tilck/kernel/hal.h>

void save_current_task_state(regs_t *r,  bool irq)
{
   struct task *curr = get_curr_task();

   ASSERT(curr != NULL);

   if (irq) {
      /*
       * In case of preemption while in userspace that happens while the
       * interrupts are disabled. Make sure we ignore that fact while saving
       * the current state and always keep the IF flag set in the EFLAGS
       * register.
       */
#if defined(__i386__)
      r->eflags |= EFLAGS_IF;
#elif defined (__x86_64__)
      r->rflags |= EFLAGS_IF;
#elif defined(__riscv)
      r->sstatus |= SR_SPIE;
#elif defined(KERNEL_TEST)
      /* do nothing, that's OK */
#else
      #error Not implemented
#endif

   }

   curr->state_regs = r;
}


int save_regs_on_user_stack(regs_t *r)
{
   ulong user_sp = regs_get_usersp(r);
   int rc;

   /* Align the user ESP */
   user_sp &= ALIGNED_MASK(USERMODE_STACK_ALIGN);

   /* Allocate space on the user stack */
   user_sp -= sizeof(*r);

   /* Save the registers to the user stack */
   rc = copy_to_user(TO_PTR(user_sp), r, sizeof(*r));

   if (rc) {
      /* Oops, stack overflow */
      return -EFAULT;
   }

   /* Now, after we saved the registers, update useresp */
   regs_set_usersp(r, user_sp);
   return 0;
}

void restore_regs_from_user_stack(regs_t *r)
{
   ulong old_regs = regs_get_usersp(r);
   int rc;

   /* Restore the registers we previously changed */
   rc = copy_from_user(r, TO_PTR(old_regs), sizeof(*r));

   if (rc) {
      /* Oops, something really weird happened here */
      enable_preemption();
      terminate_process(0, SIGSEGV);
      NOT_REACHED();
   }

#if defined(__i386__)
   r->cs = X86_USER_CODE_SEL;
   r->eflags |= EFLAGS_IF;
#elif defined(__x86_64__)
   NOT_IMPLEMENTED();
#elif defined(__riscv)
   r->sstatus |= SR_SPIE;
#elif defined(KERNEL_TEST)
      /* do nothing, that's OK */
#else
      #error Not implemented
#endif
}

void setup_pause_trampoline(regs_t *r)
{
#if defined(__x86_64__) || defined(KERNEL_TEST)
   NOT_IMPLEMENTED();
#else
   regs_set_ip(r, pause_trampoline_user_vaddr);
#endif
}

void
switch_to_task_safety_checks(struct task *curr, struct task *next)
{
   static bool first_task_switch_passed;
   bool cond;

   /*
    * Generally, we don't support task switches with interrupts disabled
    * simply because the current task might have ended up in the scheduler
    * by mistake, while doing a critical operation. That looks weird, but
    * why not checking against that? We have so far only *ONE* legit case
    * where entering in switch_to_task() is intentional: the first task
    * switch in kmain() to the init processs.
    *
    * In case it turns out that there are more *legit* cases where we need
    * switch to a new task with interrupts disabled, we might fix those cases
    * or decide to support that use-case, by replacing the checks below with
    * forced setting of the EFLAGS_IF bit:
    *
    *    state->eflags |= EFLAGS_IF
    *
    * For the moment, that is not necessary.
    */
   if (UNLIKELY(!are_interrupts_enabled())) {

      /*
       * Interrupts are disabled in this corner case: it's totally safe to read
       * and write the static boolean.
       */
      if (!first_task_switch_passed) {

         first_task_switch_passed = true;

      } else {

         /*
          * Oops! We're not in the first task switch and interrupts are
          * disabled: very likely there's a bug!
          */
         panic("Cannot switch away from task with interrupts disabled");
      }
   }

#if defined(__i386__)
   cond = !(next->state_regs->eflags & EFLAGS_IF);
#elif defined(__x86_64__)
   cond = !(next->state_regs->rflags & EFLAGS_IF);
#elif defined(__riscv)
   cond = !(next->state_regs->sstatus & SR_SIE) &&
          !(next->state_regs->sstatus & SR_SPIE);
#else
   cond = false;
#endif

   /*
    * Make sure in NO WAY we'll switch to a user task keeping interrupts
    * disabled. That would be a disaster. And if that happens due to a weird
    * bug, let's try to learn as much as possible about why that happened.
    */
   if (UNLIKELY(cond)) {

      const char *curr_str =
         curr->kthread_name
            ? curr->kthread_name
            : curr->pi->debug_cmdline;

      const char *next_str =
         next->kthread_name
            ? next->kthread_name
            : next->pi->debug_cmdline;

      printk("[sched] task: %d (%p, %s) => %d (%p, %s)\n",
             curr->tid, curr, curr_str,
             next->tid, next, next_str);

      if (next->running_in_kernel) {
         dump_stacktrace(
            regs_get_frame_ptr(next->state_regs),
            next->pi->pdir
         );
      }

      panic("[sched] Next task does not have interrupts enabled. "
            "In kernel: %u, timer_ready: %u, is_sigsuspend: %u, "
            "sa_pending: %p, sa_fault_pending: %p, "
            "sa_mask: %p, sa_old_mask: %p",
            next->running_in_kernel,
            next->timer_ready,
            next->in_sigsuspend,
            next->sa_pending[0],
            next->sa_fault_pending[0],
            next->sa_mask[0],
            next->sa_old_mask[0]);
   }
}

void
set_current_task_in_user_mode(void)
{
   ASSERT(!is_preemption_enabled());
   struct task *curr = get_curr_task();

   curr->running_in_kernel &= ~((u32)IN_SYSCALL_FLAG);
   task_info_reset_kernel_stack(curr);

#if defined(__i386__)
   set_kernel_stack((u32)curr->state_regs);
#elif defined(__x86_64__)
   NOT_IMPLEMENTED();
#endif
}

int
sys_set_tid_address(int *tidptr)
{
   /*
    * NOTE: this syscall must always succeed. In case the user pointer
    * is not valid, we'll send SIGSEGV to the just created thread.
    */

   get_curr_proc()->set_child_tid = tidptr;
   return get_curr_task()->tid;
}

bool
arch_specific_new_task_setup(struct task *ti, struct task *parent)
{
   arch_task_members_t *arch = get_task_arch_fields(ti);

   if (FORK_NO_COW) {

      if (parent) {

         /*
          * We parent is set, we're forking a task and we must NOT preserve the
          * arch fields. But, if we're not forking (parent is set), it means
          * we're in execve(): in that case there's no point to reset the arch
          * fields. Actually, here, in the NO_COW case, we MUST NOT do it, in
          * order to be sure we won't fail.
          */

         bzero(arch, sizeof(arch_task_members_t));
      }

      if (arch->fpu_regs) {

         /*
          * We already have an FPU regs buffer: just clear its contents and
          * keep it allocated.
          */
         bzero(arch->fpu_regs, arch->fpu_regs_size);

      } else {

         /* We don't have a FPU regs buffer: unless this is kthread, allocate */
         if (LIKELY(!is_kernel_thread(ti)))
            if (!allocate_fpu_regs(arch))
               return false; // out-of-memory
      }

   } else {

      /*
       * We're not in the NO_COW case. We have to free the arch specific fields
       * (like the fpu_regs buffer) if the parent is NULL. Otherwise, just reset
       * its members to zero.
       */

      if (parent) {
         bzero(arch, sizeof(*arch));
      } else {
         arch_specific_free_task(ti);
      }
   }

   return true;
}

void
arch_specific_free_task(struct task *ti)
{
   arch_task_members_t *arch = get_task_arch_fields(ti);
   kfree2(arch->fpu_regs, arch->fpu_regs_size);
   arch->fpu_regs = NULL;
   arch->fpu_regs_size = 0;
}

NORETURN void
switch_to_task(struct task *ti)
{
   /* Save the value of ti->state_regs as it will be reset below */
   regs_t *state = ti->state_regs;
   struct task *curr = get_curr_task();
   bool should_drop_top_syscall = false;
   const bool zombie = (curr->state == TASK_STATE_ZOMBIE);

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

   if (!is_kernel_thread(curr) && !zombie) {
      save_curr_fpu_ctx_if_enabled();
   }

   if (!is_kernel_thread(ti)) {
      arch_usermode_task_switch(ti);
   }

   if (KRN_TRACK_NESTED_INTERR) {
      if (running_in_kernel(curr) && !is_kernel_thread(curr))
         should_drop_top_syscall = true;
   }

   if (UNLIKELY(zombie)) {
      ulong var;
      disable_interrupts(&var);
      {
         set_curr_task(kernel_process);
      }
      enable_interrupts(&var);
      free_mem_for_zombie_task(curr);
   }

   /* From here until the end, we have to be as fast as possible */
   disable_interrupts_forced();

   if (KRN_TRACK_NESTED_INTERR) {
      if (should_drop_top_syscall)
         nested_interrupts_drop_top_syscall();
   }

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

