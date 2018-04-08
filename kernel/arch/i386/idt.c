
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <exos/hal.h>

#include "idt_int.h"

extern void (*isr_entry_points[32])(void);
void syscall_int80_entry(void);

static idt_entry idt[256];
static interrupt_handler fault_handlers[32];

void load_idt(idt_entry *entries, u32 entries_count)
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
 * We set the access flags to 0x8E. This means that the entry is
 * present, is running in ring 0 (kernel level), and has the lower 5 bits
 * set to the required '14', which is represented by 'E' in hex.
 */

static void isrs_install(void)
{
   for (int i = 0; i < 32; i++)
      idt_set_entry(i, isr_entry_points[i], 0x08, 0x8E);

   // Syscall with int 0x80.

   // Note: flags is 0xEE, in order to allow this interrupt
   // to be used from ring 3.

   // Flags:
   // P | DPL | Always 01110 (14)
   // P = Segment is present, 1 = Yes
   // DPL = Ring
   //
   idt_set_entry(0x80, syscall_int80_entry, 0x08, 0xEE);
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

void setup_soft_interrupt_handling(void)
{
   isrs_install();
   load_idt(idt, ARRAY_SIZE(idt));
}
