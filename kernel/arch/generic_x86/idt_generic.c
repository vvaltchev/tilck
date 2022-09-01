/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/process.h>

soft_int_handler_t fault_handlers[32];

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

void set_fault_handler(int ex_num, void *ptr)
{
   fault_handlers[ex_num] = (soft_int_handler_t) ptr;
}

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
      x86_exception_names[int_num], int_num, r->err_code, regs_get_ip(r));

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
               regs_get_ip(r));
      }
   }
}
