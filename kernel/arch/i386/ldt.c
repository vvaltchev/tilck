
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <exos/errno.h>
#include <exos/process.h>

#include "gdt_int.h"

typedef struct {
   u32 entry_number;
   uptr base_addr;
   u32 limit;
   u32 seg_32bit:1;
   u32 contents:2;
   u32 read_exec_only:1;
   u32 limit_in_pages:1;
   u32 seg_not_present:1;
   u32 useable:1;
} user_desc;

void load_ldt(u32 entry_index_in_gdt, u32 dpl)
{
   ASSERT(!are_interrupts_enabled());

   asmVolatile("lldt %w0"
               : /* no output */
               : "q" (X86_SELECTOR(entry_index_in_gdt, TABLE_GDT, dpl))
               : "memory");
}

sptr sys_set_thread_area(user_desc *d)
{
   printk("[kernel] set_thread_area(e: %i,\n"
          "                         base: %p,\n"
          "                         lim: %p,\n"
          "                         32-bit: %u,\n"
          "                         contents: %u,\n"
          "                         re_only: %u,\n"
          "                         lim in pag: %u,\n"
          "                         seg_not_pres: %u,\n"
          "                         useable: %u)\n",
          d->entry_number,
          d->base_addr,
          d->limit,
          d->seg_32bit,
          d->contents,
          d->read_exec_only,
          d->limit_in_pages,
          d->seg_not_present,
          d->useable);

   //gdt_entry *e = (gdt_entry *)&current->ldt_raw[0];

   return -ENOSYS;
}
