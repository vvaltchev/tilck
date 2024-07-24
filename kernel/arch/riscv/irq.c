/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/worker_thread.h>
#include <tilck/kernel/timer.h>

#include <tilck/mods/irqchip.h>

extern struct irq_data irq_datas[MAX_IRQ_NUM];
extern ulong irq_bitmap[MAX_IRQ_NUM / (sizeof(ulong) * 8)];

struct list irq_handlers_lists[MAX_IRQ_NUM];

void irq_set_mask(int irq)
{
   ulong var;
   u32 hwirq;
   struct irq_domain *domain;
   ASSERT(IN_RANGE_INC(irq, 0, MAX_IRQ_NUM - 1));

   disable_interrupts(&var);
   if (irq_datas[irq].present) {
      domain = irq_datas[irq].domain;
      hwirq = irq_datas[irq].hwirq;
      domain->ops->hwirq_set_mask(hwirq, domain->priv);
   }
   enable_interrupts(&var);
}

void irq_clear_mask(int irq)
{
   ulong var;
   u32 hwirq;
   struct irq_domain *domain;
   ASSERT(IN_RANGE_INC(irq, 0, MAX_IRQ_NUM - 1));

   disable_interrupts(&var);
   if (irq_datas[irq].present) {
      domain = irq_datas[irq].domain;
      hwirq = irq_datas[irq].hwirq;
      domain->ops->hwirq_clear_mask(hwirq, domain->priv);
   }
   enable_interrupts(&var);
}

bool irq_is_masked(int irq)
{
   ulong var;
   u32 hwirq;
   struct irq_domain *domain;
   bool ret;

   ASSERT(IN_RANGE_INC(irq, 0, MAX_IRQ_NUM - 1));

   disable_interrupts(&var);
   if (irq_datas[irq].present) {
      domain = irq_datas[irq].domain;
      hwirq = irq_datas[irq].hwirq;
      ret = domain->ops->hwirq_is_masked(hwirq, domain->priv);
   }
   enable_interrupts(&var);
   return ret;
}

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

int get_irq_num(regs_t *context)
{
   return int_to_irq(context->int_num);
}

int get_int_num(regs_t *context)
{
   return context->int_num;
}

void init_irq_handling(void)
{
   ASSERT(!are_interrupts_enabled());

   /* Make all irq numbers available */
   for (ulong i = 0; i < MAX_IRQ_NUM / sizeof(ulong); i++) {
      irq_bitmap[i] = 0;
   }

   /*
    * The serial port and timer interrupt numbers in tilck are fixed numbers,
    * we reserve 0 ~ 15 IRQ numbers.
    */
   irq_bitmap[0] |= 0xffffUL;

   for (int i = 0; i < MAX_IRQ_NUM; i++) {

      list_init(&irq_handlers_lists[i]);
      irq_datas[i].hwirq = 0;
      irq_datas[i].unhandled_count = 0;
      irq_datas[i].domain = 0;
      irq_datas[i].present = false;
   }

   init_fdt_irqchip();
}

