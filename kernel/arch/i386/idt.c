
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/fault_resumable.h>

#include "idt_int.h"

extern void (*fault_entry_points[32])(void);

static idt_entry idt[256];
static interrupt_handler fault_handlers[32];

void load_idt(idt_entry *entries, u32 entries_count)
{
   struct {
      u16 size_minus_one;
      idt_entry *idt_addr;
   } PACKED idt_ptr = { sizeof(idt_entry) * entries_count - 1, entries };

   asmVolatile("lidt (%0)"
               : /* no output */
               : "q" (&idt_ptr)
               : "memory");
}


void idt_set_entry(u8 num, void *handler, u16 selector, u8 flags)
{
   const u32 base = (u32) handler;

   /* The interrupt routine address (offset in the code segment) */
   idt[num].offset_low = (base & 0xFFFF);
   idt[num].offset_high = (base >> 16) & 0xFFFF;

   /* Selector of the code segment to use for the 'offset' address */
   idt[num].selector = selector;

   idt[num].always0 = 0;
   idt[num].flags = flags;
}

const char *x86_exception_names[32] =
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

void handle_resumable_fault(regs *r)
{
   task_info *curr = get_curr_task();

   ASSERT(!are_interrupts_enabled());
   pop_nested_interrupt(); // the fault
   set_return_register(curr->fault_resume_regs, 1 << regs_intnum(r));
   context_switch(curr->fault_resume_regs);
}

void handle_fault(regs *r)
{
   VERIFY(is_fault(r->int_num));

   if (LIKELY(r->int_num == FAULT_PAGE_FAULT)) {

      bool was_cow;

      enable_interrupts_forced();
      {
         was_cow = handle_potential_cow(r);
      }
      disable_interrupts_forced();

      if (was_cow)
         return;
   }

   if (is_fault_resumable(r->int_num)) {
      handle_resumable_fault(r);
      return;
   }

   if (fault_handlers[r->int_num] != NULL) {

      fault_handlers[r->int_num](r);

   } else {

      panic("Fault #%i: %s [errCode: %p]",
            r->int_num,
            x86_exception_names[r->int_num],
            r->err_code);
   }
}

void set_fault_handler(int ex_num, void *ptr)
{
   fault_handlers[ex_num] = (interrupt_handler) ptr;
}

void setup_soft_interrupt_handling(void)
{
   /* Set the entries for the x86 faults (exceptions) */
   for (int i = 0; i < 32; i++) {
      idt_set_entry(i,
                    fault_entry_points[i],
                    X86_KERNEL_CODE_SEL,
                    IDT_FLAG_PRESENT | IDT_FLAG_INT_GATE | IDT_FLAG_DPL0);
   }

   load_idt(idt, ARRAY_SIZE(idt));
}

