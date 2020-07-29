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

#include "idt_int.h"
#include "pic.h"

extern void (*irq_entry_points[16])(void);

static struct list irq_handlers_lists[16] = {
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

/*
 * We first remap the interrupt controllers, and then we install
 * the appropriate ISRs to the correct entries in the IDT. This
 * is just like installing the exception handlers.
 */

void init_irq_handling(void)
{
   ASSERT(!are_interrupts_enabled());
   init_pic_8259(32, 40);

   for (u8 i = 0; i < ARRAY_SIZE(irq_handlers_lists); i++) {
      idt_set_entry(32 + i,
                    irq_entry_points[i],
                    X86_KERNEL_CODE_SEL,
                    IDT_FLAG_PRESENT | IDT_FLAG_INT_GATE | IDT_FLAG_DPL0);
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

void arch_irq_handling(regs_t *r)
{
   enum irq_action hret = IRQ_UNHANDLED;
   const int irq = r->int_num - 32;
   struct irq_handler_node *pos;

   ASSERT(!are_interrupts_enabled());
   ASSERT(!is_preemption_enabled());

   if (pic_is_spur_irq(irq)) {
      spur_irq_count++;
      return;
   }

   push_nested_interrupt(r->int_num);
   handle_irq_set_mask(irq);
   pic_send_eoi(irq);
   enable_interrupts_forced();
   {
      list_for_each_ro(pos, &irq_handlers_lists[irq], node) {

         hret = pos->handler(pos->context);

         if (hret != IRQ_UNHANDLED)
            break;
      }

      if (hret == IRQ_UNHANDLED)
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
