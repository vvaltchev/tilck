/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/hal.h>
#include <tilck/mods/tracing.h>

void handle_syscall(regs_t *);
void handle_fault(regs_t *);
void arch_irq_handling(regs_t *r);

/*
 * Nested IRQ counter needed for in_irq().
 *
 * Note: the `__in_irq_count` mechanism tracks only nested IRQs and has NOTHING
 * to do with `KRN_TRACK_NESTED_INTERR` which is a debug util that tracks all
 * the interrupt types, including: syscalls, faults and IRQs.
 */
ATOMIC(int) __in_irq_count;

static ALWAYS_INLINE void inc_irq_count(void)
{
   atomic_fetch_add_explicit(&__in_irq_count, 1, mo_relaxed);
}

static ALWAYS_INLINE void dec_irq_count(void)
{
   DEBUG_ONLY_UNSAFE(int oldval =)
      atomic_fetch_sub_explicit(&__in_irq_count, 1, mo_relaxed);

   ASSERT(oldval > 0);
}

#if KRN_TRACK_NESTED_INTERR

static int nested_interrupts_count;
static int nested_interrupts[MAX_NESTED_INTERRUPTS] =
{
   [0 ... MAX_NESTED_INTERRUPTS-1] = -1,
};

inline void push_nested_interrupt(int int_num)
{
   ulong var;
   disable_interrupts(&var); /* under #if KRN_TRACK_NESTED_INTERR */
   {
      ASSERT(nested_interrupts_count < MAX_NESTED_INTERRUPTS);
      ASSERT(nested_interrupts_count >= 0);
      nested_interrupts[nested_interrupts_count++] = int_num;
   }
   enable_interrupts(&var);
}

inline void pop_nested_interrupt(void)
{
   ulong var;
   disable_interrupts(&var); /* under #if KRN_TRACK_NESTED_INTERR */
   {
      nested_interrupts_count--;
      ASSERT(nested_interrupts_count >= 0);
   }
   enable_interrupts(&var);
}

bool in_nested_irq_num(int irq_num)
{
   ASSERT(!are_interrupts_enabled());

   for (int i = nested_interrupts_count - 2; i >= 0; i--)
      if (int_to_irq(nested_interrupts[i]) == irq_num)
         return true;

   return false;
}

void check_not_in_irq_handler(void)
{
   if (!in_panic()) {
      if (in_irq())
         panic("NOT expected to be in an IRQ handler");
   }
}

void check_in_irq_handler(void)
{
   if (!in_panic()) {
      if (!in_irq())
         panic("Expected TO BE in an IRQ handler (but we're NOT)");
   }
}

void check_in_no_other_irq_than_timer(void)
{
   ulong var;

   if (in_panic())
      return;

   disable_interrupts(&var); /* under #if KRN_TRACK_NESTED_INTERR */
   {
      if (nested_interrupts_count > 0) {

         int n = nested_interrupts[nested_interrupts_count - 1];

         if (is_irq(n) && !is_timer_irq(n))
            panic("NOT expected to be in an IRQ handler != IRQ0 [timer]");
      }
   }
   enable_interrupts(&var);
}

static void DEBUG_check_not_same_interrupt_nested(int int_num)
{
   ASSERT(!are_interrupts_enabled());

   for (int i = nested_interrupts_count - 1; i >= 0; i--)
      if (nested_interrupts[i] == int_num) {

         if (int_num == 32) {

            /*
             * Tolerate nested IRQ 0 for debug purposes: to make sure that the
             * timer handler *never* gets so slow that IRQ #0 gets fired again
             * before it finishes.
             */
            return;
         }

         panic("Same interrupt (%i) twice in nested_interrupts[]", int_num);
      }
}


void nested_interrupts_drop_top_syscall(void)
{
   if (nested_interrupts_count > 0) {
      ASSERT(nested_interrupts_count == 1);
      ASSERT(nested_interrupts[0] == SYSCALL_SOFT_INTERRUPT);
      pop_nested_interrupt();
   }
}

void panic_dump_nested_interrupts(void)
{
   VERIFY(in_panic());

   if (!kopt_panic_kb) {
      ASSERT(!are_interrupts_enabled());
   }

   char buf[128];
   int written = 0;

   written += snprintk(buf + written, sizeof(buf), "Interrupts: [ ");

   for (int i = nested_interrupts_count - 1; i >= 0; i--) {
      written += snprintk(buf + written, sizeof(buf) - (u32)written,
                          "%i ", nested_interrupts[i]);
   }

   /* written += */ snprintk(buf + written, sizeof(buf) - (u32) written, "]\n");
   printk("%s", buf);
}

#else

static ALWAYS_INLINE void DEBUG_check_not_same_interrupt_nested(int n) { }

#endif // KRN_TRACK_NESTED_INTERR

static void irq_resched(regs_t *r)
{
   /* Check if we just disabled the preemption or it was disabled before */
   if (get_preempt_disable_count() == 1) {

      /* It wasn't disabled before: save the current state (registers) */
      save_current_task_state(r, true /* irq */);

      /* Re-enable the interrupts, keeping preemption disabled */
      enable_interrupts_forced();

      /* Call do_schedule() with preemption disabled, as mandatory */
      do_schedule();
   }
}

void irq_entry(regs_t *r)
{
   ASSERT(get_curr_task() != NULL);
   DEBUG_check_not_same_interrupt_nested(regs_intnum(r));

   /* We expect here that the CPU disabled the interrupts */
   ASSERT(!are_interrupts_enabled());

   trace_printk(1, "IRQ entry %d", regs_intnum(r) - 32);

   /* Disable the preemption */
   disable_preemption();

   /* Increase the always-enabled in_irq_count counter */
   inc_irq_count();

   /* Call the arch-dependent IRQ handling logic */
   arch_irq_handling(r);

   /* Decrease the always-enabled in_irq_count counter */
   dec_irq_count();

   /* Check that arch_irq_handling restored the interrupts state to disabled */
   ASSERT(!are_interrupts_enabled());

   /* Check that the preemption is disabled as well */
   ASSERT(!is_preemption_enabled());

   trace_printk(1, "IRQ exit %d", regs_intnum(r)- 32);

   /* Run the scheduler if necessary (it will enable interrupts) */
   // if (need_reschedule()) {
   //    trace_printk(1, "IRQ resched");
   //    irq_resched(r);
   // }

   /*
    * In case do_schedule() returned or there was no need for resched, just
    * re-enable the preemption and return.
    */
   enable_preemption_nosched();
}

void syscall_entry(regs_t *r)
{
   /*
    * Interrupts are disabled in order to have a slightly better tracking of
    * the nested interrupts (debug feature). The preemption must always be
    * enabled here.
    */
   ASSERT(!are_interrupts_enabled());
   ASSERT(is_preemption_enabled());

   push_nested_interrupt(0x80);
   disable_preemption();
   //enable_interrupts_forced();
   {
      handle_syscall(r);
   }
   //disable_interrupts_forced();
   enable_preemption_nosched();
   pop_nested_interrupt();

   ASSERT(is_preemption_enabled());
}

void fault_entry(regs_t *r)
{
   /*
    * Here preemption could be either enabled or disabled. Typically is enabled,
    * but it's totally possible for example a page fault to occur in the kernel
    * while preemption is disabled.
    */
   ASSERT(!are_interrupts_enabled());

   trace_printk(1, "fault entry %d", regs_intnum(r));
   push_nested_interrupt(regs_intnum(r));
   disable_preemption();
   enable_interrupts_forced();

   handle_fault(r);

   /*
    * Pop the nested interrupt and process signals while preemption is
    * still disabled.
    */
   pop_nested_interrupt();
   process_signals(get_curr_task(), sig_in_fault, r);

   trace_printk(1, "fault exit %d", regs_intnum(r));
   enable_preemption();
   disable_interrupts_forced();
}

