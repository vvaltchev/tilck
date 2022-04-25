/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/atomics.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/hal_types.h>

void set_fault_handler(int fault, void *ptr);

static ALWAYS_INLINE bool in_irq(void)
{
   extern ATOMIC(int) __in_irq_count;
   return atomic_load_explicit(&__in_irq_count, mo_relaxed) > 0;
}

#if KRN_TRACK_NESTED_INTERR
   void check_not_in_irq_handler(void);
   void check_in_irq_handler(void);
   void push_nested_interrupt(int int_num);
   void pop_nested_interrupt(void);
   void nested_interrupts_drop_top_syscall(void);
   void panic_dump_nested_interrupts(void);
   void check_in_no_other_irq_than_timer(void);
   bool in_nested_irq_num(int irq_num);
#else
   static inline void check_not_in_irq_handler(void) { }
   static inline void check_in_irq_handler(void) { }
   static inline void push_nested_interrupt(int int_num) { }
   static inline void pop_nested_interrupt(void) { }
   static inline void nested_interrupts_drop_top_syscall(void) { }
   static inline void panic_dump_nested_interrupts(void) { }
   static inline void check_in_no_other_irq_than_timer(void) { }
   static inline bool in_nested_irq_num(int irq_num) { NOT_REACHED(); }
#endif
