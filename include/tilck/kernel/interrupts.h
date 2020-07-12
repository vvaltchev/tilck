/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/list.h>
#include <tilck/kernel/hal_types.h>

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

void set_fault_handler(int fault, void *ptr);
void exit_fault_handler_state(void);

#if KRN_TRACK_NESTED_INTERR
void check_not_in_irq_handler(void);
void check_in_irq_handler(void);
void push_nested_interrupt(int int_num);
void pop_nested_interrupt(void);
void nested_interrupts_drop_top_syscall(void);
void panic_dump_nested_interrupts(void);
void check_in_no_other_irq_than_timer(void);

/* The following value-returning funcs are NOT defined in the #else case: */
bool in_syscall(void);
bool in_nested_irq_num(int irq_num);
int get_nested_interrupts_count(void);

#else
static inline void check_not_in_irq_handler(void) { }
static inline void check_in_irq_handler(void) { }
static inline void push_nested_interrupt(int int_num) { }
static inline void pop_nested_interrupt(void) { }
static inline void nested_interrupts_drop_top_syscall(void) { }
static inline void panic_dump_nested_interrupts(void) { }
static inline void check_in_no_other_irq_than_timer(void) { }
#endif
