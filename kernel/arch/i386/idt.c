
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <exos/hal.h>

#include "idt_int.h"

static idt_entry idt[256];

void idt_load(idt_entry *entries, u32 entries_count)
{
   struct {
      u16 offset_of_last_byte; /* a.k.a total_size - 1 */
      idt_entry *idt_addr;
   } PACKED idt_ptr = { sizeof(idt_entry) * entries_count - 1, entries };

   asmVolatile("lidt (%0)"
               : /* no output */
               : "q" (&idt_ptr)
               : "memory");
}


void idt_set_entry(u8 num, void *handler, u16 sel, u8 flags)
{
   const u32 base = (u32)handler;

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
 * by Intel and are designed to service exceptions.
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

// This is used for int 0x80 (syscalls)
void isr128();

/*
 * We set the access flags to 0x8E. This means that the entry is
 * present, is running in ring 0 (kernel level), and has the lower 5 bits
 * set to the required '14', which is represented by 'E' in hex.
 */

void isrs_install()
{
   idt_set_entry(0, isr0, 0x08, 0x8E);
   idt_set_entry(1, isr1, 0x08, 0x8E);
   idt_set_entry(2, isr2, 0x08, 0x8E);
   idt_set_entry(3, isr3, 0x08, 0x8E);
   idt_set_entry(4, isr4, 0x08, 0x8E);
   idt_set_entry(5, isr5, 0x08, 0x8E);
   idt_set_entry(6, isr6, 0x08, 0x8E);
   idt_set_entry(7, isr7, 0x08, 0x8E);

   idt_set_entry(8, isr8, 0x08, 0x8E);
   idt_set_entry(9, isr9, 0x08, 0x8E);
   idt_set_entry(10, isr10, 0x08, 0x8E);
   idt_set_entry(11, isr11, 0x08, 0x8E);
   idt_set_entry(12, isr12, 0x08, 0x8E);
   idt_set_entry(13, isr13, 0x08, 0x8E);
   idt_set_entry(14, isr14, 0x08, 0x8E);
   idt_set_entry(15, isr15, 0x08, 0x8E);

   idt_set_entry(16, isr16, 0x08, 0x8E);
   idt_set_entry(17, isr17, 0x08, 0x8E);
   idt_set_entry(18, isr18, 0x08, 0x8E);
   idt_set_entry(19, isr19, 0x08, 0x8E);
   idt_set_entry(20, isr20, 0x08, 0x8E);
   idt_set_entry(21, isr21, 0x08, 0x8E);
   idt_set_entry(22, isr22, 0x08, 0x8E);
   idt_set_entry(23, isr23, 0x08, 0x8E);

   idt_set_entry(24, isr24, 0x08, 0x8E);
   idt_set_entry(25, isr25, 0x08, 0x8E);
   idt_set_entry(26, isr26, 0x08, 0x8E);
   idt_set_entry(27, isr27, 0x08, 0x8E);
   idt_set_entry(28, isr28, 0x08, 0x8E);
   idt_set_entry(29, isr29, 0x08, 0x8E);
   idt_set_entry(30, isr30, 0x08, 0x8E);
   idt_set_entry(31, isr31, 0x08, 0x8E);

   // Syscall with int 0x80.

   // Note: flags is 0xEE, in order to allow this interrupt
   // to be used from ring 3.

   // Flags:
   // P | DPL | Always 01110 (14)
   // P = Segment is present, 1 = Yes
   // DPL = Ring
   //
   idt_set_entry(0x80, isr128, 0x08, 0xEE);
}

const char *exception_messages[] =
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

static interrupt_handler fault_handlers[32];


void handle_fault(regs *r)
{
   if (fault_handlers[r->int_num] != NULL) {

      fault_handlers[r->int_num](r);

   } else {

      panic("Fault #%i: %s [errCode: %i]",
            r->int_num,
            exception_messages[r->int_num],
            r->err_code);
   }
}

void set_fault_handler(int ex_num, void *ptr)
{
   fault_handlers[ex_num] = (interrupt_handler) ptr;
}


/* Installs the IDT */
void idt_install(void)
{
   /* Add any new ISRs to the IDT here using idt_set_entry */
   isrs_install();

   /* Points the processor's internal register to the new IDT */
   idt_load(idt, ARRAY_SIZE(idt));
}
