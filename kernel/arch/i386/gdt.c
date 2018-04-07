
#include <common/basic_defs.h>
#include <common/string_util.h>

#define GDT_LIMIT_MAX (0x000FFFFF)

typedef struct
{
   u16 limit_low;
   u16 base_low;
   u8 base_middle;
   u8 access;
   u8 limit_high: 4;
   u8 granularity: 4;
   u8 base_high;

} PACKED gdt_entry;

// A struct describing a Task State Segment.
typedef struct
{
   u32 prev_tss;   // The previous TSS - if we used hardware task switching this
                   // would form a linked list.
   u32 esp0;       // The stack pointer to load when we change to kernel mode.
   u32 ss0;        // The stack segment to load when we change to kernel mode.
   u32 esp1;       // everything below here is unusued now..
   u32 ss1;
   u32 esp2;
   u32 ss2;
   u32 cr3;
   u32 eip;
   u32 eflags;
   u32 eax;
   u32 ecx;
   u32 edx;
   u32 ebx;
   u32 esp;
   u32 ebp;
   u32 esi;
   u32 edi;
   u32 es;
   u32 cs;
   u32 ss;
   u32 ds;
   u32 fs;
   u32 gs;
   u32 ldt;
   u16 trap;
   u16 iomap_base;
} PACKED tss_entry_t;


static gdt_entry gdt[6];

void gdt_set_entry(int num,
                   uptr base,
                   uptr limit,
                   u8 access,
                   u8 gran)
{
   ASSERT(limit <= GDT_LIMIT_MAX); /* limit is only 20 bits */
   gdt[num].base_low = (base & 0xFFFF);
   gdt[num].base_middle = (base >> 16) & 0xFF;
   gdt[num].base_high = (base >> 24) & 0xFF;

   gdt[num].limit_low = (limit & 0xFFFF);
   gdt[num].limit_high = ((limit >> 16) & 0x0F);

   gdt[num].access = access;
   gdt[num].granularity = (gran >> 4);
}

/*
 * ExOS does use i386's tasks because they do not exist in many architectures.
 * Therefore, we have just a single TSS entry.
 */
static tss_entry_t tss_entry;

void set_kernel_stack(u32 stack)
{
   tss_entry.esp0 = stack;
}

u32 get_kernel_stack()
{
   return tss_entry.esp0;
}

static void tss_init(tss_entry_t *entry, u16 ss0, u32 esp0)
{
   tss_entry.ss0 = ss0;   // Set the kernel stack segment.
   tss_entry.esp0 = esp0; // Set the kernel stack pointer.

   tss_entry.cs = 0x08 + 3;
   tss_entry.ds = 0x10 + 3;
   tss_entry.ss = 0x10 + 3;
   tss_entry.es = tss_entry.fs = tss_entry.gs = 0x10 + 3;
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
   gdt_set_entry(1, 0, GDT_LIMIT_MAX, 0x9A, 0xCF);

   /*
    * The third entry is our Data Segment. It's EXACTLY the
    * same as our code segment, but the descriptor type in
    * this entry's access byte says it's a Data Segment.
    */
   gdt_set_entry(2, 0, GDT_LIMIT_MAX, 0x92, 0xCF);


   gdt_set_entry(3, 0, GDT_LIMIT_MAX, 0xFA, 0xCF); // User mode code segment
   gdt_set_entry(4, 0, GDT_LIMIT_MAX, 0xF2, 0xCF); // User mode data segment

   tss_init(&tss_entry, 0x10, 0x0);
   gdt_set_entry(5, (u32) &tss_entry, sizeof(tss_entry), 0xE9, 0x00);


   gdt_load(gdt, 6);
   load_tss(5, 3);
}
