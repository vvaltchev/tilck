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

   save_fp_ra
   save_callee_regs

   la t0, .kernel_yield_resume
   REG_S t0, 37 * RISCV_SZPTR(sp) # save kernel_resume_pc

   mv a0, sp
   li a1, 0
   call save_current_task_state

#if DEBUG_CHECKS && KRN_TRACK_NESTED_INTERR
   call check_not_in_irq_handler
#endif

   call do_schedule

   # do_schedule() just returned: restore SP and just return

   addi sp, sp, SIZEOF_REGS

   li a0, 0      # No context-switch return value

   restore_fp_ra
   ret

.kernel_yield_resume:

   resume_callee_regs

   li a0, 1      # Context-switch return value
   restore_fp_ra
   ret

END_FUNC(asm_save_regs_and_schedule)

FUNC(panic_save_current_state):

   save_fp_ra
   save_all_regs

   mv a0, sp
   call panic_save_current_task_state
   addi sp, sp, SIZEOF_REGS

   restore_fp_ra
   ret

END_FUNC(panic_save_current_state)

FUNC(switch_to_initial_kernel_stack):

   # save the fp-sp offset
   mv t0, s0
   sub t0, s0, sp

   # change the stack pointer and the frame pointer
   la s0, kernel_initial_stack + ASM_KERNEL_STACK_SZ
   mv sp, s0

   # restore the fp-sp difference
   sub sp, sp, t0
   ret

END_FUNC(switch_to_initial_kernel_stack)

