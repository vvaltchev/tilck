
#include <commonDefs.h>
#include <stringUtil.h>
#include <term.h>

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

/*
 * Declare an IDT of 256 entries. Although we will only use the
 * first 32 entries in this tutorial, the rest exists as a bit
 * of a trap. If any undefined IDT entry is hit, it normally
 * will cause an "Unhandled Interrupt" exception. Any descriptor
 * for which the 'presence' bit is cleared (0) will generate an
 * "Unhandled Interrupt" exception
 */
struct idt_entry idt[256];
struct idt_ptr idtp;

/* This exists in 'start.asm', and is used to load our IDT */
extern void idt_load();

/*
 * Use this function to set an entry in the IDT. Alot simpler
 * than twiddling with the GDT ;)
 */
void idt_set_gate(unsigned char num,
                  unsigned long base,
                  unsigned short sel,
                  unsigned char flags)
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

/*
 * These are function prototypes for all of the exception
 * handlers: The first 32 entries in the IDT are reserved
 * by Intel, and are designed to service exceptions!
 */

void isr0();
void isr1();
void isr2();
void isr3();
void isr4();
void isr5();
void isr6();
void isr7();
void isr8();
void isr9();
void isr10();
void isr11();
void isr12();
void isr13();
void isr14();
void isr15();
void isr16();
void isr17();
void isr18();
void isr19();
void isr20();
void isr21();
void isr22();
void isr23();
void isr24();
void isr25();
void isr26();
void isr27();
void isr28();
void isr29();
void isr30();
void isr31();


void isr128();

/*
 * This is a very repetitive function... it's not hard, it's
 * just annoying. As you can see, we set the first 32 entries
 * in the IDT to the first 32 ISRs. We can't use a for loop
 * for this, because there is no way to get the function names
 * that correspond to that given entry. We set the access
 * flags to 0x8E. This means that the entry is present, is
 * running in ring 0 (kernel level), and has the lower 5 bits
 * set to the required '14', which is represented by 'E' in
 * hex.
 */

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

   // Syscall with int 0x80.

   // Note: flags is 0xEE, in order to allow this interrupt
   // to be used from ring 3.

   // Flags:
   // P | DPL | Always 01110 (14)
   // P = Segment is present, 1 = Yes
   // DPL = Ring
   //
   idt_set_gate(0x80, (unsigned)isr128, 0x08, 0xEE);
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


int handle_syscall(struct regs *);

void *fault_handlers[32] = { NULL };

void set_fault_handler(int exceptionNum, void *ptr)
{
   fault_handlers[exceptionNum] = ptr;
}

int generic_interrupt_handler(struct regs *r)
{
   if (LIKELY(r->int_no == 0x80)) {
      return handle_syscall(r);
   }

   // Higher exception numbers are handled by irq_handler()
   ASSERT(r->int_no < 32);

   void(*handler)(struct regs *r);
   handler = fault_handlers[r->int_no];

   if (handler) {

      handler(r);

   } else {

      cli();

      printk("Fault #%i: %s [errCode: %i]\n",
             r->int_no,
             exception_messages[r->int_no],
             r->err_code);
      halt();
   }

   return 0;
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
