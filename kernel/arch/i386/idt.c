/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/process.h>

#include "idt_int.h"

void handle_gpf(regs_t *r);
void handle_ill(regs_t *r);
void handle_div0(regs_t *r);
void handle_cpf(regs_t *r);

extern void (*fault_entry_points[32])(void);

static struct idt_entry idt[256];
static soft_int_handler_t fault_handlers[32];

void load_idt(struct idt_entry *entries, u16 entries_count)
{
   struct {
      u16 size_minus_one;
      struct idt_entry *idt_addr;
   } PACKED idt_ptr = { sizeof(struct idt_entry) * entries_count - 1, entries };

   asmVolatile("lidt (%0)"
               : /* no output */
               : "q" (&idt_ptr)
               : "memory");
}


void idt_set_entry(u8 num, void *handler, u16 selector, u8 flags)
{
   const u32 base = (u32) handler;

   /* The interrupt routine address (offset in the code segment) */
   idt[num].offset_low = LO_BITS(base, 16, u16);
   idt[num].offset_high = LO_BITS(base >> 16, 16, u16);

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
   "Reserved",
};

void handle_resumable_fault(regs_t *r)
{
   struct task *curr = get_curr_task();
   pop_nested_interrupt(); // the fault
   set_return_register(curr->fault_resume_regs, 1u << regs_intnum(r));
   context_switch(curr->fault_resume_regs);
}

static void fault_in_panic(regs_t *r)
{
   const int int_num = r->int_num;

   if (is_fault_resumable(int_num))
      return handle_resumable_fault(r);

   /*
    * We might be so unlucky that printk() causes some fault(s) too: therefore,
    * not even trying to print something on the screen is safe. In order to
    * avoid generating an endless sequence of page faults in the worst case,
    * just call printk() in SAFE way here.
    */
   fault_resumable_call(
      ALL_FAULTS_MASK, printk, 5,
      "FATAL: %s [%d] while in panic state [E: 0x%x, EIP: %p]\n",
      x86_exception_names[int_num], int_num, r->err_code, r->eip);

   /* Halt the CPU forever */
   while (true) { halt(); }
}

void handle_fault(regs_t *r)
{
   const int int_num = r->int_num;
   bool cow = false;

   ASSERT(is_fault(int_num));

   if (UNLIKELY(in_panic()))
      return fault_in_panic(r);

   if (LIKELY(int_num == FAULT_PAGE_FAULT)) {
      cow = handle_potential_cow(r);
   }

   if (!cow) {

      if (is_fault_resumable(int_num))
         return handle_resumable_fault(r);

      if (LIKELY(fault_handlers[int_num] != NULL)) {

         fault_handlers[int_num](r);

      } else {

         panic("Unhandled fault #%i: %s [err: %p] EIP: %p",
               int_num,
               x86_exception_names[int_num],
               r->err_code,
               r->eip);
      }
   }
}

void set_fault_handler(int ex_num, void *ptr)
{
   fault_handlers[ex_num] = (soft_int_handler_t) ptr;
}

static void handle_debugbreak(regs_t *r)
{
   /*
    * Do nothing, literally. The purpose of this way of handling `int 3` is to
    * allow during development to easily put a breakpoint in user-space code and
    * from GDB (using remote debugging) to put a HW breakpoint here.
    *
    * On Linux, the behavior of `int 3` is quite different, but that's fine.
    * This interrupt is anyway used only for debugging purposes. In the future,
    * it is absolutely possible that Tilck will handle it in a different way.
    * For the moment, the kernel offers no debugging features of userspace
    * programs (= no such thing as ptrace).
    */
   asmVolatile("nop");
}

void init_cpu_exception_handling(void)
{
   /* Set the entries for the x86 faults (exceptions) */
   for (u8 i = 0; i < 32; i++) {
      idt_set_entry(i,
                    fault_entry_points[i],
                    X86_KERNEL_CODE_SEL,
                    IDT_FLAG_PRESENT | IDT_FLAG_INT_GATE | IDT_FLAG_DPL0);
   }

   /*
    * Set a special entry for the breakpoint FAULT, allowing `int 3` to work
    * from userspace, without triggering a GPF, in the same way int 0x80 is
    * allowed by init_syscall_interfaces().
    */
   idt_set_entry(FAULT_BREAKPOINT,
                 fault_entry_points[FAULT_BREAKPOINT],
                 X86_KERNEL_CODE_SEL,
                 IDT_FLAG_PRESENT | IDT_FLAG_INT_GATE | IDT_FLAG_DPL3);

   load_idt(idt, ARRAY_SIZE(idt));
   set_fault_handler(FAULT_GENERAL_PROTECTION, handle_gpf);
   set_fault_handler(FAULT_INVALID_OPCODE, handle_ill);
   set_fault_handler(FAULT_DIVISION_BY_ZERO, handle_div0);
   set_fault_handler(FAULT_COPROC_FAULT, handle_cpf);
   set_fault_handler(FAULT_BREAKPOINT, handle_debugbreak);
}

