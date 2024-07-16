/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/list.h>

#define MAX_IRQ_NUM 256

extern const char *riscv_exception_names[32];
extern struct list irq_handlers_lists[MAX_IRQ_NUM];
extern soft_int_handler_t fault_handlers[32];

static ALWAYS_INLINE int int_to_irq(int int_num)
{
   return int_num >= 32 ? int_num - 32 : -1;
}

static ALWAYS_INLINE bool is_irq(int int_num)
{
   return int_num >= 32;
}

static ALWAYS_INLINE bool is_timer_irq(int int_num)
{
   return int_to_irq(int_num) == IRQ_S_TIMER;
}

static ALWAYS_INLINE bool is_fault(int int_num)
{
   return IN_RANGE(int_num, 0, 32);
}
