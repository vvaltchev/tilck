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


struct list irq_handlers_lists[MAX_IRQ_NUM];

void irq_set_mask(int irq)
{
   NOT_IMPLEMENTED();
}

void irq_clear_mask(int irq)
{
   NOT_IMPLEMENTED();
}

bool irq_is_masked(int irq)
{
   NOT_IMPLEMENTED();
}

/* This installs a custom IRQ handler for the given IRQ */
void irq_install_handler(u8 irq, struct irq_handler_node *n)
{
   NOT_IMPLEMENTED();
}

/* This clears the handler for a given IRQ */
void irq_uninstall_handler(u8 irq, struct irq_handler_node *n)
{
   NOT_IMPLEMENTED();
}

int get_irq_num(regs_t *context)
{
   NOT_IMPLEMENTED();
}

int get_int_num(regs_t *context)
{
   NOT_IMPLEMENTED();
}

void init_irq_handling(void)
{
   NOT_IMPLEMENTED();
}
