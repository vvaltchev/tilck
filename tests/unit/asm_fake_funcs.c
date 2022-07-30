/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

// Defining some necessary symbols just to make the linker happy.

void *kernel_initial_stack = NULL;

void asm_save_regs_and_schedule() { NOT_REACHED(); }
void switch_to_initial_kernel_stack() { NOT_REACHED(); }
void fault_resumable_call() { NOT_REACHED(); }
void asm_do_bogomips_loop(void) { NOT_REACHED(); }
void asm_nop_loop(void) { NOT_REACHED(); }
