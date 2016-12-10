
#pragma once

#include <commonDefs.h>

#define FAULT_DIVISION_BY_ZERO     0
#define FAULT_DEBUG                1
#define FAULT_NMI                  2
#define FAULT_BREAKPOINT           3
#define FAULT_INTO_DEC_OVERFLOW    4
#define FAULT_OUT_OF_BOUNDS        5
#define FAULT_INVALID_OPCODE       6
#define FAULT_NO_COPROC            7

#define FAULT_DOUBLE_FAULT         8
#define FAULT_COPROC_SEG_OVERRRUN  9
#define FAULT_BAD_TSS             10
#define FAULT_SEG_NOT_PRESENT     11
#define FAULT_STACK_FAULT         12
#define FAULT_GENERAL_PROTECTION  13
#define FAULT_PAGE_FAULT          14
#define FAULT_UNKNOWN_INTERRUPT   15
#define FAULT_COPROC_FAULT        16
#define FAULT_ALIGN_FAULT         17
#define FAULT_MACHINE_CHECK       18

// Forward-declaring regs
typedef struct regs regs;

void irq_install();

void irq_install_handler(u8 irq, void(*handler)(regs *r));
void irq_uninstall_handler(u8 irq);

void IRQ_set_mask(u8 IRQline);
void IRQ_clear_mask(u8 IRQline);

void set_fault_handler(int fault, void *ptr);
