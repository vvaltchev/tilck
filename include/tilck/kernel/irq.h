/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/hal_types.h>
#include <tilck/kernel/interrupts.h>

struct irq_handler_node {

   struct list_node node;
   irq_handler_t handler;
   void *context;          /* device-specific context, passed to the handler */
};

#define DEFINE_IRQ_HANDLER_NODE(node_name, func, ctx)        \
   static struct irq_handler_node node_name = {              \
      .node = make_list_node(node_name.node),                \
      .handler = (func),                                     \
      .context = (ctx),                                      \
   };


void init_irq_handling();

void irq_install_handler(u8 irq, struct irq_handler_node *n);
void irq_uninstall_handler(u8 irq, struct irq_handler_node *n);

void irq_set_mask(int irq);
void irq_clear_mask(int irq);
