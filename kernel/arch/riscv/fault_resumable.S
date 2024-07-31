# SPDX-License-Identifier: BSD-2-Clause

#define ASM_FILE 1

#include <tilck_gen_headers/config_global.h>
#include <tilck/kernel/arch/riscv/asm_defs.h>

.section .text

.global fault_resumable_call

FUNC(fault_resumable_call):

   save_fp_ra

   addi sp, sp, -4 * RISCV_SZPTR

   la t2, __disable_preempt           # push __disable_preempt
   lw t1, (t2)
   sw t1, 0 * RISCV_SZPTR(sp)

   la t0, __current
   REG_L t0, (t0)
   REG_L t1, TI_F_RESUME_RS_OFF(t0)
   REG_S t1, 2 * RISCV_SZPTR(sp)      # push current->fault_resume_regs

   lw t1, TI_FAULTS_MASK_OFF(t0)
   sw t1, 1 * RISCV_SZPTR(sp)         # push current->faults_resume_mask

   save_callee_regs

   la t2, .asm_fault_resumable_call_resume
   REG_S t2, 37 * RISCV_SZPTR(sp)     # save kernel_resume_pc

   la t0, __current
   REG_L t0, (t0)
   REG_S sp, TI_F_RESUME_RS_OFF(t0)   # save to current->fault_resume_regs

   sw a0, TI_FAULTS_MASK_OFF(t0)      # arg1: faults_mask
   mv s1, a1                          # arg2: func
   mv s2, a2                          # arg3: nargs

   // Place the first five parameters in A0 to A4

   addi s2, s2, -5
   mv a0, a3
   mv a1, a4
   mv a2, a5
   mv a3, a6
   mv a4, a7

   // Take the next three parameters from the stack
   // and place them in A5 to A7

   mv s3, s0
   mv s5, sp                         # backup sp

   blez s2, 2f
   addi s2, s2, -1
   REG_L a5, (s3)

   blez s2, 2f
   addi s2, s2, -1
   addi s3, s3, RISCV_SZPTR
   REG_L a6, (s3)

   blez s2, 2f
   addi s2, s2, -1
   addi s3, s3, RISCV_SZPTR
   REG_L a7, (s3)

   // Take the remaining parameters from the stack and
   // put them on the stack

   blez s2, 2f
   slli s4, s2, RISCV_LOGSZPTR
   mv s5, sp                         # backup sp
   sub sp, sp, s4
   andi sp, sp, ~(0xf)
   mv s4, sp

1:
   addi s2, s2, -1
   addi s3, s3, RISCV_SZPTR
   REG_L t0, (s3)
   REG_S t0, (s4)
   addi s4, s4, RISCV_SZPTR
   bgtz s2, 1b

2:
   jalr s1
   mv a0, zero                       # return value: set to 0 (= no faults)
   mv sp, s5

   resume_callee_regs

   la t0, __current
   REG_L t0, (t0)
   lw t1, 1 * RISCV_SZPTR(sp)
   sw t1, TI_FAULTS_MASK_OFF(t0)     # pop current->faults_resume_mask

   REG_L t1, 2 * RISCV_SZPTR(sp)
   REG_S t1, TI_F_RESUME_RS_OFF(t0)  # pop current->fault_resume_regs
   addi sp, sp, 4 * RISCV_SZPTR

   restore_fp_ra
   ret

.asm_fault_resumable_call_resume:

   REG_L a0, 10 * RISCV_SZPTR(sp)    # extracted a0 value which
                                     # set in handle_resumable_fault()
   resume_callee_regs

   la t0, __current
   REG_L t0, (t0)
   lw t1, 1 * RISCV_SZPTR(sp)
   sw t1, TI_FAULTS_MASK_OFF(t0)

   REG_L t1, 2 * RISCV_SZPTR(sp)
   REG_S t1, TI_F_RESUME_RS_OFF(t0)

   la t0, __disable_preempt          # pop __disable_preempt
   lw t1, 0 * RISCV_SZPTR(sp)
   sw t1, (t0)
   addi sp, sp, 4 * RISCV_SZPTR

   restore_fp_ra
   ret

END_FUNC(fault_resumable_call)

