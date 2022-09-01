# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1

#include <tilck_gen_headers/config_global.h>
#include <tilck_gen_headers/config_kernel.h>
#include <tilck_gen_headers/config_debug.h>

#include <tilck/kernel/arch/x86_64/asm_defs.h>
#include <multiboot.h>

.section .text

.code64

.global asm_save_regs_and_schedule
.global panic_save_current_state
.global switch_to_initial_kernel_stack

# Saves the current (kernel) state as if an interrupt occurred while running
# in kernel mode.

FUNC(asm_save_regs_and_schedule):

   # TODO: implement this
   ret

.kernel_yield_resume:

   # TODO: implement this
   ret

END_FUNC(asm_save_regs_and_schedule)

FUNC(panic_save_current_state):

   # TODO: implement this
   ret

END_FUNC(panic_save_current_state)

FUNC(switch_to_initial_kernel_stack):

   # TODO: implement this
   ret

END_FUNC(switch_to_initial_kernel_stack)

