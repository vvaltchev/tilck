
#include <commonDefs.h>
#include <stringUtil.h>
#include <term.h>

/* bkerndev - Bran's Kernel Development Tutorial
*  By:   Brandon F. (friesenb@gmail.com)
*  Desc: Interrupt Descriptor Table management
*
*  Notes: No warranty expressed or implied. Use at own risk. */

/* Defines an IDT entry */
struct idt_entry
{
    unsigned short base_lo;
    unsigned short sel;
    unsigned char always0;
    unsigned char flags;
    unsigned short base_hi;
} __attribute__((packed));

struct idt_ptr
{
    unsigned short limit;
    void *base;
} __attribute__((packed));

/* Declare an IDT of 256 entries. Although we will only use the
*  first 32 entries in this tutorial, the rest exists as a bit
*  of a trap. If any undefined IDT entry is hit, it normally
*  will cause an "Unhandled Interrupt" exception. Any descriptor
*  for which the 'presence' bit is cleared (0) will generate an
*  "Unhandled Interrupt" exception */
struct idt_entry idt[256];
struct idt_ptr idtp;

/* This exists in 'start.asm', and is used to load our IDT */
extern void idt_load();

/* Use this function to set an entry in the IDT. Alot simpler
*  than twiddling with the GDT ;) */
void idt_set_gate(unsigned char num, unsigned long base, unsigned short sel, unsigned char flags)
{
    /* The interrupt routine's base address */
    idt[num].base_lo = (base & 0xFFFF);
    idt[num].base_hi = (base >> 16) & 0xFFFF;

    /* The segment or 'selector' that this IDT entry will use
    *  is set here, along with any access flags */
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

/* These are function prototypes for all of the exception
*  handlers: The first 32 entries in the IDT are reserved
*  by Intel, and are designed to service exceptions! */
extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();

/* This is a very repetitive function... it's not hard, it's
*  just annoying. As you can see, we set the first 32 entries
*  in the IDT to the first 32 ISRs. We can't use a for loop
*  for this, because there is no way to get the function names
*  that correspond to that given entry. We set the access
*  flags to 0x8E. This means that the entry is present, is
*  running in ring 0 (kernel level), and has the lower 5 bits
*  set to the required '14', which is represented by 'E' in
*  hex. */
void isrs_install()
{
   idt_set_gate(0, (unsigned)isr0, 0x08, 0x8E);
   idt_set_gate(1, (unsigned)isr1, 0x08, 0x8E);
   idt_set_gate(2, (unsigned)isr2, 0x08, 0x8E);
   idt_set_gate(3, (unsigned)isr3, 0x08, 0x8E);
   idt_set_gate(4, (unsigned)isr4, 0x08, 0x8E);
   idt_set_gate(5, (unsigned)isr5, 0x08, 0x8E);
   idt_set_gate(6, (unsigned)isr6, 0x08, 0x8E);
   idt_set_gate(7, (unsigned)isr7, 0x08, 0x8E);

   idt_set_gate(8, (unsigned)isr8, 0x08, 0x8E);
   idt_set_gate(9, (unsigned)isr9, 0x08, 0x8E);
   idt_set_gate(10, (unsigned)isr10, 0x08, 0x8E);
   idt_set_gate(11, (unsigned)isr11, 0x08, 0x8E);
   idt_set_gate(12, (unsigned)isr12, 0x08, 0x8E);
   idt_set_gate(13, (unsigned)isr13, 0x08, 0x8E);
   idt_set_gate(14, (unsigned)isr14, 0x08, 0x8E);
   idt_set_gate(15, (unsigned)isr15, 0x08, 0x8E);

   idt_set_gate(16, (unsigned)isr16, 0x08, 0x8E);
   idt_set_gate(17, (unsigned)isr17, 0x08, 0x8E);
   idt_set_gate(18, (unsigned)isr18, 0x08, 0x8E);
   idt_set_gate(19, (unsigned)isr19, 0x08, 0x8E);
   idt_set_gate(20, (unsigned)isr20, 0x08, 0x8E);
   idt_set_gate(21, (unsigned)isr21, 0x08, 0x8E);
   idt_set_gate(22, (unsigned)isr22, 0x08, 0x8E);
   idt_set_gate(23, (unsigned)isr23, 0x08, 0x8E);

   idt_set_gate(24, (unsigned)isr24, 0x08, 0x8E);
   idt_set_gate(25, (unsigned)isr25, 0x08, 0x8E);
   idt_set_gate(26, (unsigned)isr26, 0x08, 0x8E);
   idt_set_gate(27, (unsigned)isr27, 0x08, 0x8E);
   idt_set_gate(28, (unsigned)isr28, 0x08, 0x8E);
   idt_set_gate(29, (unsigned)isr29, 0x08, 0x8E);
   idt_set_gate(30, (unsigned)isr30, 0x08, 0x8E);
   idt_set_gate(31, (unsigned)isr31, 0x08, 0x8E);
}

/* This is a simple string array. It contains the message that
*  corresponds to each and every exception. We get the correct
*  message by accessing like:
*  exception_message[interrupt_number] */
char *exception_messages[] =
{
   "Division By Zero",
   "Debug",
   "Non Maskable Interrupt",
   "Breakpoint",
   "Into Detected Overflow",
   "Out of Bounds",
   "Invalid Opcode",
   "No Coprocessor",

   "Double Fault",
   "Coprocessor Segment Overrun",
   "Bad TSS",
   "Segment Not Present",
   "Stack Fault",
   "General Protection Fault",
   "Page Fault",
   "Unknown Interrupt",

   "Coprocessor Fault",
   "Alignment Check",
   "Machine Check",
   "Reserved",
   "Reserved",
   "Reserved",
   "Reserved",
   "Reserved",

   "Reserved",
   "Reserved",
   "Reserved",
   "Reserved",
   "Reserved",
   "Reserved",
   "Reserved",
   "Reserved"
};


/* All of our Exception handling Interrupt Service Routines will
*  point to this function. This will tell us what exception has
*  happened! Right now, we simply halt the system by hitting an
*  endless loop. All ISRs disable interrupts while they are being
*  serviced as a 'locking' mechanism to prevent an IRQ from
*  happening and messing up kernel data structures */
void fault_handler(struct regs *r)
{
   /* Is this a fault whose number is from 0 to 31? */
   if (r->int_no < 32)
   {
      /*  Display the description for the Exception that occurred.
      *  In this tutorial, we will simply halt the system using an
      *  infinite loop.
      */

      cli();

      char buf[32];

      write_string("Exception N#: ");
      itoa(r->int_no, buf, 10);
      write_string(buf);
      write_string(", ");
      write_string(exception_messages[r->int_no]);
      write_string("\n");

      write_string("Error code: ");
      itoa(r->err_code, buf, 10);
      write_string(buf);
      write_string("\n");

      halt();
   }
}



/* Installs the IDT */
void idt_install()
{
    /* Sets the special IDT pointer up, just like in 'gdt.c' */
    idtp.limit = (sizeof (struct idt_entry) * 256) - 1;
    idtp.base = &idt;

    /* Clear out the entire IDT, initializing it to zeros */
    memset(&idt, 0, sizeof(struct idt_entry) * 256);

    /* Add any new ISRs to the IDT here using idt_set_gate */

    isrs_install();

    /* Points the processor's internal register to the new IDT */
    idt_load();
}
