
#include <common_defs.h>
#include <string_util.h>

void gdt_load();
void tss_flush();

struct gdt_entry
{
    u16 limit_low;
    u16 base_low;
    u8 base_middle;
    u8 access;
    u8 granularity;
    u8 base_high;
} __attribute__((packed));

struct gdt_entry_bits
{
   u32 limit_low:16;
   u32 base_low : 24;
   //attribute byte split into bitfields
   u32 accessed :1;
   u32 read_write :1; //readable for code, writable for data
   u32 conforming_expand_down :1; //conforming for code, expand down for data
   u32 code :1; //1 for code, 0 for data
   u32 always_1 :1; //should be 1 for everything but TSS and LDT
   u32 DPL :2; //priviledge level
   u32 present :1;
   //and now into granularity
   u32 limit_high :4;
   u32 available :1;
   u32 always_0 :1; //should always be 0
   u32 big :1; //32bit opcodes for code, u32 stack for data
   u32 gran :1; //1 to use 4k page addressing, 0 for byte addressing
   u32 base_high :8;
} __attribute__((packed));

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


/*
 * Special pointer which includes the limit: The max bytes
 * taken up by the GDT, minus 1. This NEEDS to be packed.
 */

struct gdt_ptr
{
   u16 limit; // 2 bytes
   u32 base;  // 4 bytes
} __attribute__((packed));

/* Our GDT, with 3 entries, and finally our special GDT pointer */
struct gdt_entry gdt[6];
struct gdt_ptr gdt_pointer;

/* Setup a descriptor in the Global Descriptor Table */
void gdt_set_gate(int num,
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



// Initialise our task state segment structure.
static void write_tss(s32 num, u16 ss0, u32 esp0)
{
    // Firstly, let's compute the base and limit of our entry into the GDT.
    u32 base = (u32) &tss_entry;
    u32 limit = base + sizeof(tss_entry);

    // Now, add our TSS descriptor's address to the GDT.
    gdt_set_gate(num, base, limit, 0xE9, 0x00);

    // Ensure the descriptor is initially zero.
    memset(&tss_entry, 0, sizeof(tss_entry));

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
    tss_entry.cs   = 0x0b;
    tss_entry.ss =
      tss_entry.ds = tss_entry.es = tss_entry.fs = tss_entry.gs = 0x13;
}


/* Should be called by main. This will setup the special GDT
*  pointer, set up the first 3 entries in our GDT, and then
*  finally call gdt_flush() in our assembler file in order
*  to tell the processor where the new GDT is and update the
*  new segment registers */
void gdt_install()
{
   /* Setup the GDT pointer and limit */
   gdt_pointer.limit = (sizeof(struct gdt_entry) * 6) - 1;
   gdt_pointer.base = (u32)&gdt;

   /* Our NULL descriptor */
   gdt_set_gate(0, 0, 0, 0, 0);

   /*
    * The second entry is our Code Segment. The base address
    * is 0, the limit is 4GBytes, it uses 4KByte granularity,
    * uses 32-bit opcodes, and is a Code Segment descriptor.
   */
   gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

   /*
    * The third entry is our Data Segment. It's EXACTLY the
    * same as our code segment, but the descriptor type in
    * this entry's access byte says it's a Data Segment.
    */
   gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);


   gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // User mode code segment
   gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User mode data segment

   write_tss(5, 0x10, 0x0);

   /* Flush out the old GDT and install the new changes! */
   gdt_load();

   tss_flush();
}
