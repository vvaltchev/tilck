
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>
#include <exos/kernel/errno.h>
#include <exos/kernel/process.h>

#include "gdt_int.h"

void load_ldt(u32 entry_index_in_gdt, u32 dpl)
{
   ASSERT(!are_interrupts_enabled());

   asmVolatile("lldt %w0"
               : /* no output */
               : "q" (X86_SELECTOR(entry_index_in_gdt, TABLE_GDT, dpl))
               : "memory");
}
