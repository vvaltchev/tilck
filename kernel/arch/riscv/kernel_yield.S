# SPDX-License-Identifier: BSD-2-Clause

#define ASM_FILE 1

#include <tilck_gen_headers/config_global.h>
#include <tilck_gen_headers/config_kernel.h>
#include <tilck_gen_headers/config_debug.h>

#include <tilck/kernel/arch/riscv/asm_defs.h>
#include <multiboot.h>

.section .text

.global asm_save_regs_and_schedule
.global panic_save_current_state
.global switch_to_initial_kernel_stack

# Saves the current (kernel) state as if an interrupt occurred while running
# in kernel mode.

FUNC(asm_save_regs_and_schedule):

   ret

END_FUNC(asm_save_regs_and_schedule)

FUNC(panic_save_current_state):

   ret

END_FUNC(panic_save_current_state)

FUNC(switch_to_initial_kernel_stack):

   ret

END_FUNC(switch_to_initial_kernel_stack)

