
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <exos/hal.h>

#include "gdt_int.h"

static gdt_entry gdt[6];

/*
 * ExOS does use i386's tasks because they do not exist in many architectures.
 * Therefore, we have just need a single TSS entry.
 */
static tss_entry_t tss_entry;

void gdt_set_entry(int num,
                   uptr base,
                   uptr limit,
                   u8 access,
                   u8 flags)
{
   ASSERT(limit <= GDT_LIMIT_MAX); /* limit is only 20 bits */
   ASSERT(flags <= 0xF); /* flags is 4 bits */

   gdt[num].base_low = (base & 0xFFFF);
   gdt[num].base_middle = (base >> 16) & 0xFF;
   gdt[num].base_high = (base >> 24) & 0xFF;

   gdt[num].limit_low = (limit & 0xFFFF);
   gdt[num].limit_high = ((limit >> 16) & 0x0F);

   gdt[num].access = access;
   gdt[num].flags = flags;
}


void set_kernel_stack(u32 stack)
{
   tss_entry.ss0 = X86_SELECTOR(2, 0, 0);   // Kernel stack segment [0x10]
   tss_entry.esp0 = stack;
}

u32 get_kernel_stack()
{
   return tss_entry.esp0;
}

void gdt_load(gdt_entry *gdt, u32 entries_count)
{
   struct {

      u16 offset_of_last_byte;
      gdt_entry *gdt_vaddr;

   } PACKED gdt_ptr = { sizeof(gdt_entry) * entries_count - 1, gdt };

   asmVolatile("lgdt (%0)"
               : /* no output */
               : "q" (&gdt_ptr)
               : "memory");
}

void load_tss(u32 entry_index_in_gdt, u32 dpl)
{
   ASSERT(dpl <= 3); /* descriptor privilege level [0..3] */

   /*
    * Inline assembly comments: here we need to use %w0 instead of %0
    * beacuse 'ltr' requires a 16-bit register, like AX. That's what does
    * the 'w' modifier [https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html].
    */

   asmVolatile("ltr %w0"
               : /* no output */
               : "q" (entry_index_in_gdt * sizeof(gdt_entry) | dpl)
               : "memory");
}

void gdt_install(void)
{
   /* Our NULL descriptor */
   gdt_set_entry(0, 0, 0, 0, 0);

   /*
    * The second entry is our Code Segment. The base address
    * is 0, the limit is 4GBytes, it uses 4KByte granularity,
    * uses 32-bit opcodes, and is a Code Segment descriptor.
   */
   gdt_set_entry(1, 0, GDT_LIMIT_MAX, 0x9A, GDT_REGULAR_32BIT_SEG);

   /*
    * The third entry is our Data Segment. It's EXACTLY the
    * same as our code segment, but the descriptor type in
    * this entry's access byte says it's a Data Segment.
    */
   gdt_set_entry(2, 0, GDT_LIMIT_MAX, 0x92, GDT_REGULAR_32BIT_SEG);


   gdt_set_entry(3, 0, GDT_LIMIT_MAX, 0xFA, GDT_REGULAR_32BIT_SEG); // User code
   gdt_set_entry(4, 0, GDT_LIMIT_MAX, 0xF2, GDT_REGULAR_32BIT_SEG); // User data

   gdt_set_entry(5, (uptr) &tss_entry, sizeof(tss_entry), 0xE9, 0);

   gdt_load(gdt, 6);
   load_tss(5, 3);
}
