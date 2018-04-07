
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
 * Entry-points for exception handlers. Their stub code is in isr_handlers.S.
 * The exceptions (faults) are actually handled by handle_fault() [see below].
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

static void (*ex_handlers_array[32])() =
{
   isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7, isr8, isr9, isr10, isr11,
   isr12, isr13, isr14, isr15, isr16, isr17, isr18, isr19, isr20, isr21,
   isr22, isr23, isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
};

// This is used for int 0x80 (syscalls)
void isr128();

/*
 * We set the access flags to 0x8E. This means that the entry is
 * present, is running in ring 0 (kernel level), and has the lower 5 bits
 * set to the required '14', which is represented by 'E' in hex.
 */

void isrs_install(void)
{
   for (int i = 0; i < 32; i++)
      idt_set_entry(i, ex_handlers_array[i], 0x08, 0x8E);

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
