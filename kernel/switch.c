/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

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
