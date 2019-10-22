/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

static ALWAYS_INLINE int int_to_irq(int int_num)
{
   return int_num >= 32 ? int_num - 32 : -1;
}

static ALWAYS_INLINE bool is_irq(int int_num)
{
   return int_num >= 32 && int_num != SYSCALL_SOFT_INTERRUPT;
}

static ALWAYS_INLINE bool is_timer_irq(int int_num)
{
   return int_to_irq(int_num) == X86_PC_TIMER_IRQ;
}

static ALWAYS_INLINE bool is_fault(int int_num)
{
   return IN_RANGE(int_num, 0, 32);
}
