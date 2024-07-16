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

void set_fault_handler(int ex_num, void *ptr)
{
   NOT_IMPLEMENTED();
}

void init_cpu_exception_handling(void)
{
   NOT_IMPLEMENTED();
}

void handle_resumable_fault(regs_t *r)
{
   NOT_IMPLEMENTED();
}

static void fault_in_panic(regs_t *r)
{
   NOT_IMPLEMENTED();
}

void handle_fault(regs_t *r)
{
   NOT_IMPLEMENTED();
}

void on_first_pdir_update(void)
{
   return;
}
