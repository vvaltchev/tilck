
#include <common/basic_defs.h>
#include <common/string_util.h>

void gdt_load();
void tss_flush();

typedef struct
{
    u16 limit_low;
    u16 base_low;
    u8 base_middle;
    u8 access;
    u8 granularity;
    u8 base_high;
} PACKED gdt_entry;



typedef struct gdt_entry_bits gdt_entry_bits;

// A struct describing a Task State Segment.
struct tss_entry_struct
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
} __attribute__((packed));

typedef struct tss_entry_struct tss_entry_t;



/* Our GDT, with 3 entries, and finally our special GDT pointer */
gdt_entry gdt[6];


void gdt_set_entry(int num,
                   size_t base,
                   size_t limit,
                   u8 access,
                   u8 gran)
{
    /* Setup the descriptor base address */
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;

    /* Setup the descriptor limits */
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F);

    /* Finally, set up the granularity and access flags */
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}

tss_entry_t tss_entry;

void set_kernel_stack(u32 stack)
{
   tss_entry.esp0 = stack;
}

u32 get_kernel_stack()
{
   return tss_entry.esp0;
}


// Initialize our task state segment structure.
static void write_tss(s32 num, u16 ss0, u32 esp0)
{
    // First, let's compute the base and limit of our entry into the GDT.
    u32 base = (u32) &tss_entry;
    u32 limit = base + sizeof(tss_entry);

    // Now, add our TSS descriptor's address to the GDT.
    gdt_set_entry(num, base, limit, 0xE9, 0x00);

    // Ensure the descriptor is initially zero.
    bzero(&tss_entry, sizeof(tss_entry));

    tss_entry.ss0  = ss0;  // Set the kernel stack segment.
    tss_entry.esp0 = esp0; // Set the kernel stack pointer.

    /*
     * Here we set the cs, ss, ds, es, fs and gs entries in the TSS. These
     * specify what segments should be loaded when the processor switches to
     * kernel mode. Therefore they are just our normal kernel code/data segments
     * 0x08 and 0x10 respectively, but with the last two bits set, making 0x0b
     * and 0x13. The setting of these bits sets the RPL (requested privilege
     * level) to 3, meaning that this TSS can be used to switch to kernel mode
     * from ring 3.
     */
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

void gdt_install()
{
   /* Our NULL descriptor */
   gdt_set_entry(0, 0, 0, 0, 0);

   /*
    * The second entry is our Code Segment. The base address
    * is 0, the limit is 4GBytes, it uses 4KByte granularity,
    * uses 32-bit opcodes, and is a Code Segment descriptor.
   */
   gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

   /*
    * The third entry is our Data Segment. It's EXACTLY the
    * same as our code segment, but the descriptor type in
    * this entry's access byte says it's a Data Segment.
    */
   gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);


   gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // User mode code segment
   gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User mode data segment

   write_tss(5, 0x10, 0x0);


   gdt_load(gdt, 6);
   tss_flush();
}
