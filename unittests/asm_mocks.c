/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

// Defining some necessary symbols just to make the linker happy.

void *kernel_initial_stack = NULL;

void asm_kernel_yield() { NOT_REACHED(); }
void switch_to_initial_kernel_stack() { NOT_REACHED(); }
void fault_resumable_call() { NOT_REACHED(); }
