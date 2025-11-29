/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#ifndef __riscv
   #error This header can be used only when building for riscv.
#endif

static ALWAYS_INLINE ulong get_stack_ptr(void)
{
   register ulong current_sp __asm__ ("sp");

   return current_sp;
}
