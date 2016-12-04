
#include <commonDefs.h>
#include <stringUtil.h>

void gdt_load();
void tss_flush();


/* Defines a GDT entry. We say packed, because it prevents the
*  compiler from doing things that it thinks is best: Prevent
*  compiler "optimization" by packing */
struct gdt_entry
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_entry_bits
{
	uint32_t limit_low:16;
	uint32_t base_low : 24;
     //attribute byte split into bitfields
	uint32_t accessed :1;
	uint32_t read_write :1; //readable for code, writable for data
	uint32_t conforming_expand_down :1; //conforming for code, expand down for data
	uint32_t code :1; //1 for code, 0 for data
	uint32_t always_1 :1; //should be 1 for everything but TSS and LDT
	uint32_t DPL :2; //priviledge level
	uint32_t present :1;
     //and now into granularity
	uint32_t limit_high :4;
	uint32_t available :1;
	uint32_t always_0 :1; //should always be 0
	uint32_t big :1; //32bit opcodes for code, uint32_t stack for data
	uint32_t gran :1; //1 to use 4k page addressing, 0 for byte addressing
	uint32_t base_high :8;
} __attribute__((packed));

typedef struct gdt_entry_bits gdt_entry_bits;

// A struct describing a Task State Segment.
struct tss_entry_struct
{
   uint32_t prev_tss;   // The previous TSS - if we used hardware task switching this would form a linked list.
   uint32_t esp0;       // The stack pointer to load when we change to kernel mode.
   uint32_t ss0;        // The stack segment to load when we change to kernel mode.
   uint32_t esp1;       // everything below here is unusued now.. 
   uint32_t ss1;
   uint32_t esp2;
   uint32_t ss2;
   uint32_t cr3;
   uint32_t eip;
   uint32_t eflags;
   uint32_t eax;
   uint32_t ecx;
   uint32_t edx;
   uint32_t ebx;
   uint32_t esp;
   uint32_t ebp;
   uint32_t esi;
   uint32_t edi;
   uint32_t es;         
   uint32_t cs;        
   uint32_t ss;        
   uint32_t ds;        
   uint32_t fs;       
   uint32_t gs;         
   uint32_t ldt;      
   uint16_t trap;
   uint16_t iomap_base;
} __attribute__((packed));

typedef struct tss_entry_struct tss_entry_t;


/* Special pointer which includes the limit: The max bytes
*  taken up by the GDT, minus 1. Again, this NEEDS to be packed */
struct gdt_ptr
{
   uint16_t limit; // 2 bytes
   uint32_t base;  // 4 bytes
} __attribute__((packed));

/* Our GDT, with 3 entries, and finally our special GDT pointer */
struct gdt_entry gdt[6];
struct gdt_ptr gdt_pointer;

/* Setup a descriptor in the Global Descriptor Table */
void gdt_set_gate(int num,
                  size_t base,
                  size_t limit,
                  uint8_t access,
                  uint8_t gran)
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

void set_kernel_stack(uint32_t stack) //this will update the ESP0 stack used when an interrupt occurs
{
   tss_entry.esp0 = stack;
}



// Initialise our task state segment structure.
static void write_tss(int32_t num, uint16_t ss0, uint32_t esp0)
{
    // Firstly, let's compute the base and limit of our entry into the GDT.
    uint32_t base = (uint32_t) &tss_entry;
    uint32_t limit = base + sizeof(tss_entry);
    
    // Now, add our TSS descriptor's address to the GDT.
    gdt_set_gate(num, base, limit, 0xE9, 0x00);

    // Ensure the descriptor is initially zero.
    memset(&tss_entry, 0, sizeof(tss_entry));

    tss_entry.ss0  = ss0;  // Set the kernel stack segment.
    tss_entry.esp0 = esp0; // Set the kernel stack pointer.
    
    // Here we set the cs, ss, ds, es, fs and gs entries in the TSS. These specify what 
    // segments should be loaded when the processor switches to kernel mode. Therefore
    // they are just our normal kernel code/data segments - 0x08 and 0x10 respectively,
    // but with the last two bits set, making 0x0b and 0x13. The setting of these bits
    // sets the RPL (requested privilege level) to 3, meaning that this TSS can be used
    // to switch to kernel mode from ring 3.
    tss_entry.cs   = 0x0b;     
    tss_entry.ss = tss_entry.ds = tss_entry.es = tss_entry.fs = tss_entry.gs = 0x13;
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
   gdt_pointer.base = (uint32_t)&gdt;

   /* Our NULL descriptor */
   gdt_set_gate(0, 0, 0, 0, 0);

   /* The second entry is our Code Segment. The base address
   *  is 0, the limit is 4GBytes, it uses 4KByte granularity,
   *  uses 32-bit opcodes, and is a Code Segment descriptor.
   *  Please check the table above in the tutorial in order
   *  to see exactly what each value means */
   gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

   /* The third entry is our Data Segment. It's EXACTLY the
   *  same as our code segment, but the descriptor type in
   *  this entry's access byte says it's a Data Segment */
   gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);


   gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // User mode code segment
   gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User mode data segment

   write_tss(5, 0x10, 0x0);

   /* Flush out the old GDT and install the new changes! */
   gdt_load();

   tss_flush();
}
