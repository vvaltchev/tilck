/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/process.h>

void asm_trap_entry(void);
void handle_generic_fault_int(regs_t *r, const char *fault_name);
void handle_inst_illegal_fault_int(regs_t *r, const char *fault_name);
void handle_bus_fault_int(regs_t *r, const char *fault_name);

soft_int_handler_t fault_handlers[32];

const char *riscv_exception_names[32] =
{
   "Instruction Address Misaligned",
   "Instruction Access Fault",
   "Illegal Instruction",
   "Breakpoint",
   "Load Address Misaligned",
   "Load Access Fault",
   "Store(or AMO) Address Misaligned",
   "Store(or AMO) Access Fault",
   "Environment Call from U-mode",
   "Environment Call from S-mode",
   "Environment Call from H-mode",
   "Environment Call from M-mode",
   "Instruction page fault",
   "Load page fault",
   "Reserved",
   "Store(or AMO) page fault",
};

static void handle_generic_fault(regs_t *r)
{
   const int int_num = r->int_num;
   handle_generic_fault_int(r, riscv_exception_names[int_num]);
}

static void handle_inst_illegal_fault(regs_t *r)
{
   const int int_num = r->int_num;
   handle_inst_illegal_fault_int(r, riscv_exception_names[int_num]);
}

static void handle_bus_fault(regs_t *r)
{
   const int int_num = r->int_num;
   handle_bus_fault_int(r, riscv_exception_names[int_num]);
}

static void handle_breakpoint(regs_t *r)
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

void set_fault_handler(int ex_num, void *ptr)
{
   fault_handlers[ex_num] = (soft_int_handler_t) ptr;
}

void init_cpu_exception_handling(void)
{
   csr_write(CSR_STVEC, &asm_trap_entry);

   set_fault_handler(EXC_INST_MISALIGNED, handle_bus_fault);
   set_fault_handler(EXC_INST_ACCESS, handle_generic_fault);
   set_fault_handler(EXC_INST_ILLEGAL, handle_inst_illegal_fault);
   set_fault_handler(EXC_BREAKPOINT, handle_breakpoint);
   set_fault_handler(EXC_LOAD_MISALIGNED, handle_bus_fault);
   set_fault_handler(EXC_LOAD_ACCESS, handle_generic_fault);
   set_fault_handler(EXC_STORE_MISALIGNED, handle_bus_fault);
   set_fault_handler(EXC_STORE_ACCESS, handle_generic_fault);
}

void handle_resumable_fault(regs_t *r)
{
   struct task *curr = get_curr_task();

   pop_nested_interrupt(); // the fault
   disable_interrupts_forced();
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
      "FATAL: %s [%d] while in panic state [EIP: %p]\n",
      riscv_exception_names[int_num], int_num, regs_get_ip(r));

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

   if (LIKELY((int_num == EXC_INST_PAGE_FAULT) ||
              (int_num == EXC_LOAD_PAGE_FAULT) ||
              (int_num == EXC_STORE_PAGE_FAULT))) {

      cow = handle_potential_cow(r);
   }

   if (!cow) {

      if (is_fault_resumable(int_num))
         return handle_resumable_fault(r);

      if (LIKELY(fault_handlers[int_num] != NULL)) {

         fault_handlers[int_num](r);

      } else {

         panic("Unhandled fault #%i: %s EIP: %p",
               int_num,
               riscv_exception_names[int_num],
               regs_get_ip(r));
      }
   }
}

void on_first_pdir_update(void)
{
   /* do nothing */
}

