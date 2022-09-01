/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/worker_thread.h>
#include <tilck/kernel/timer.h>

#include "pic.h"

struct list irq_handlers_lists[16] = {
   STATIC_LIST_INIT(irq_handlers_lists[ 0]),
   STATIC_LIST_INIT(irq_handlers_lists[ 1]),
   STATIC_LIST_INIT(irq_handlers_lists[ 2]),
   STATIC_LIST_INIT(irq_handlers_lists[ 3]),
   STATIC_LIST_INIT(irq_handlers_lists[ 4]),
   STATIC_LIST_INIT(irq_handlers_lists[ 5]),
   STATIC_LIST_INIT(irq_handlers_lists[ 6]),
   STATIC_LIST_INIT(irq_handlers_lists[ 7]),
   STATIC_LIST_INIT(irq_handlers_lists[ 8]),
   STATIC_LIST_INIT(irq_handlers_lists[ 9]),
   STATIC_LIST_INIT(irq_handlers_lists[10]),
   STATIC_LIST_INIT(irq_handlers_lists[11]),
   STATIC_LIST_INIT(irq_handlers_lists[12]),
   STATIC_LIST_INIT(irq_handlers_lists[13]),
   STATIC_LIST_INIT(irq_handlers_lists[14]),
   STATIC_LIST_INIT(irq_handlers_lists[15]),
};

u32 unhandled_irq_count[256];
u32 spur_irq_count;

void idt_set_entry(u8 num, void *handler, u16 sel, u8 flags);

/* This installs a custom IRQ handler for the given IRQ */
void irq_install_handler(u8 irq, struct irq_handler_node *n)
{
   ulong var;
   disable_interrupts(&var);
   {
      list_add_tail(&irq_handlers_lists[irq], &n->node);
   }
   enable_interrupts(&var);
   irq_clear_mask(irq);
}

/* This clears the handler for a given IRQ */
void irq_uninstall_handler(u8 irq, struct irq_handler_node *n)
{
   ulong var;
   disable_interrupts(&var);
   {
      list_remove(&n->node);

      if (list_is_empty(&irq_handlers_lists[irq]))
         irq_set_mask(irq);
   }
   enable_interrupts(&var);
}

static inline void handle_irq_set_mask_and_eoi(int irq)
{
   if (KRN_TRACK_NESTED_INTERR) {

      /*
       * We can really allow nested IRQ0 only if we track the nested interrupts,
       * otherwise, the timer handler won't be able to know it's running in a
       * nested way and "bad things may happen".
       */

      if (irq != X86_PC_TIMER_IRQ)
         pic_mask_and_send_eoi(irq);
      else
         pic_send_eoi(irq);

   } else {
      pic_mask_and_send_eoi(irq);
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

void arch_irq_handling(regs_t *r)
{
   enum irq_action hret = IRQ_NOT_HANDLED;
   const int irq = r->int_num - 32;
   struct irq_handler_node *pos;

   ASSERT(!are_interrupts_enabled());
   ASSERT(!is_preemption_enabled());

   if (pic_is_spur_irq(irq)) {
      spur_irq_count++;
      return;
   }

   push_nested_interrupt(r->int_num);
   handle_irq_set_mask_and_eoi(irq);
   enable_interrupts_forced();
   {
      list_for_each_ro(pos, &irq_handlers_lists[irq], node) {

         hret = pos->handler(pos->context);

         if (hret != IRQ_NOT_HANDLED)
            break;
      }

      if (hret == IRQ_NOT_HANDLED)
         unhandled_irq_count[irq]++;
   }
   disable_interrupts_forced();
   handle_irq_clear_mask(irq);
   pop_nested_interrupt();
}

int get_irq_num(regs_t *context)
{
   return int_to_irq(context->int_num);
}

int get_int_num(regs_t *context)
{
   return context->int_num;
}
