/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/errno.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/paging.h>

#include "gdt_int.h"

void load_ldt(u32 entry_index_in_gdt, u32 dpl)
{
   ASSERT(!are_interrupts_enabled());

   asmVolatile("lldt %w0"
               : /* no output */
               : "q" (X86_SELECTOR(entry_index_in_gdt, TABLE_GDT, dpl))
               : "memory");
}
