/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/timer.h>

#include "pic.c.h"

extern void (*irq_entry_points[16])(void);

static list irq_handlers_lists[16] = {
   make_list(irq_handlers_lists[ 0]),
   make_list(irq_handlers_lists[ 1]),
   make_list(irq_handlers_lists[ 2]),
   make_list(irq_handlers_lists[ 3]),
   make_list(irq_handlers_lists[ 4]),
   make_list(irq_handlers_lists[ 5]),
   make_list(irq_handlers_lists[ 6]),
   make_list(irq_handlers_lists[ 7]),
   make_list(irq_handlers_lists[ 8]),
   make_list(irq_handlers_lists[ 9]),
   make_list(irq_handlers_lists[10]),
   make_list(irq_handlers_lists[11]),
   make_list(irq_handlers_lists[12]),
   make_list(irq_handlers_lists[13]),
   make_list(irq_handlers_lists[14]),
   make_list(irq_handlers_lists[15]),
};

u32 unhandled_irq_count[256];
u32 spur_irq_count;

void idt_set_entry(u8 num, void *handler, u16 sel, u8 flags);

/* This installs a custom IRQ handler for the given IRQ */
void irq_install_handler(u8 irq, irq_handler_node *n)
{
   list_add_tail(&irq_handlers_lists[irq], &n->node);
   irq_clear_mask(irq);
}

/* This clears the handler for a given IRQ */
void irq_uninstall_handler(u8 irq, irq_handler_node *n)
{
   list_remove(&n->node);
}

void irq_set_mask(int irq)
{
   u16 port;
   u8 irq_mask;
   ASSERT(0 <= irq && irq <= 32);

   if (irq < 8) {
      port = PIC1_DATA;
   } else {
      port = PIC2_DATA;
      irq -= 8;
   }

   irq_mask = inb(port);
   irq_mask |= (1 << irq);
   outb(port, irq_mask);
}

void irq_clear_mask(int irq)
{
   u16 port;
   ASSERT(0 <= irq && irq <= 32);

   if (irq < 8) {
      port = PIC1_DATA;
   } else {
      port = PIC2_DATA;
      irq -= 8;
   }

   outb(port, inb(port) & ~(1 << irq));
}

/*
 * We first remap the interrupt controllers, and then we install
 * the appropriate ISRs to the correct entries in the IDT. This
 * is just like installing the exception handlers.
 */

void init_irq_handling(void)
{
   pic_remap(32, 40);

   for (u8 i = 0; i < ARRAY_SIZE(irq_handlers_lists); i++) {
      idt_set_entry(32 + i, irq_entry_points[i], 0x08, 0x8E);
      irq_set_mask(i);
   }
}

static inline void handle_irq_set_mask(int irq)
{
   if (KRN_TRACK_NESTED_INTERR) {

      /*
       * We can really allow nested IRQ0 only if we track the nested interrupts,
       * otherwise, the timer handler won't be able to know it's running in a
       * nested way and "bad things may happen".
       */

      if (irq != X86_PC_TIMER_IRQ)
         irq_set_mask(irq);

   } else {
      irq_set_mask(irq);
   }
}

static inline void handle_irq_clear_mask(int irq)
{
   if (KRN_TRACK_NESTED_INTERR) {

      if (irq != X86_PC_TIMER_IRQ)
         irq_clear_mask(irq);

   } else {
      irq_clear_mask(irq);
   }
}

static inline bool is_spur_irq(int irq)
{
   if (irq == 7 || irq == 15) {

      /*
       * Check for a spurious wake-up.
       *
       * Source: https://wiki.osdev.org/8259_PIC, with some editing.
       *
       * When an IRQ occurs, the PIC chip tells the CPU (via. the PIC's INTR
       * line) that there's an interrupt, and the CPU acknowledges this and
       * waits for the PIC to send the interrupt vector. This creates a race
       * condition: if the IRQ disappears after the PIC has told the CPU there's
       * an interrupt but before the PIC has sent the interrupt vector to the
       * CPU, then the CPU will be waiting for the PIC to tell it which
       * interrupt vector but the PIC won't have a valid interrupt vector to
       * tell the CPU.
       *
       * To get around this, the PIC tells the CPU a fake interrupt number.
       * This is a spurious IRQ. The fake interrupt number is the lowest
       * priority interrupt number for the corresponding PIC chip (IRQ 7 for the
       * master PIC, and IRQ 15 for the slave PIC).
       *
       * Handling Spurious IRQs
       * -------------------------
       *
       * For a spurious IRQ, there is no real IRQ and the PIC chip's ISR
       * (In Service Register) flag for the corresponding IRQ will NOT be set.
       * This means that the interrupt handler must not send an EOI back to the
       * PIC to reset the ISR flag, EXCEPT when the spurious IRQ comes from the
       * 2nd PIC: in that case an EOI must be sent to the master PIC, but NOT
       * to the slave PIC.
       */

      if (!(pic_get_isr() & (1 << irq))) {

         if (irq == 15)
             pic_send_eoi(7);

         spur_irq_count++;
         return true;
      }
   }

   return false;
}

static inline void run_sched_if_possible(regs *r)
{
   disable_preemption();

   if (disable_preemption_count > 1) {

      /*
       * Preemption was already disabled: we cannot run the "bottom half" of
       * this interrupt handler right now. The scheduler will run it as soon as
       * possible.
       */

      enable_preemption(); // restore the counter
      return;
   }

   save_current_task_state(r);

   /*
    * We call here schedule with curr_irq = -1 because we are actually
    * OUTSIDE the interrupt context (see the pop_nested_interrupt() above()).
    * At the moment, only timer_irq_handler() calls schedule() from a proper
    * interrupt context. NOTE: this might change in the future.
    */
   schedule_outside_interrupt_context();

   /* In case schedule() returned, we MUST re-enable the preemption */
   enable_preemption();
}

void handle_irq(regs *r)
{
   enum irq_action hret = IRQ_UNHANDLED;
   const int irq = r->int_num - 32;

   if (is_spur_irq(irq))
      return;

   handle_irq_set_mask(irq);
   disable_preemption();
   push_nested_interrupt(r->int_num);
   ASSERT(!are_interrupts_enabled());

   /*
    * We MUST send EOI to the PIC here, before starting the interrupt handler
    * otherwise, the PIC will just not allow nested interrupts to happen.
    * NOTE: we MUST send the EOI **before** re-enabling the interrupts,
    * otherwise we'll start getting a lot of spurious interrupts!
    */
   pic_send_eoi(irq);
   enable_interrupts_forced();

   {
      irq_handler_node *pos;

      list_for_each_ro(pos, &irq_handlers_lists[irq], node) {

         if ((hret = pos->handler(r)) != IRQ_UNHANDLED)
            break;
      }
   }

   if (hret == IRQ_UNHANDLED)
      unhandled_irq_count[irq]++;

   pop_nested_interrupt();
   enable_preemption();
   handle_irq_clear_mask(irq);

   if (hret == IRQ_REQUIRES_BH) {
      /* NOTE: we are NOT in "interrupt context" anymore */
      run_sched_if_possible(r);
   }
}

int get_irq_num(regs *context)
{
   return int_to_irq(context->int_num);
}

int get_int_num(regs *context)
{
   return context->int_num;
}
