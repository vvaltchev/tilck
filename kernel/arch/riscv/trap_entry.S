# SPDX-License-Identifier: BSD-2-Clause

#define ASM_FILE 1
#include <tilck_gen_headers/config_global.h>
#include <tilck/kernel/arch/riscv/asm_defs.h>

.section .text
.global asm_trap_entry
.global asm_trap_entry_resume
.global context_switch

.align 3
FUNC(asm_trap_entry):

   # load kernel sp from sscratch, if is zero
   # then it indicates trap is from kernel
   csrrw sp, sscratch, sp
   bnez sp, .from_user

.from_kernel:
   csrrw sp, sscratch, sp

.from_user:
   save_all_regs

   la ra, asm_trap_entry_resume
   REG_S ra, 37 * RISCV_SZPTR(sp)   # save kernel_resume_pc

   csrw sscratch, zero              # sscratch always equal zero in kernel

   # Disable the FPU in kernel space
   li t0, SR_FS
   csrc sstatus, t0

   # save framepointer, then we can trace back
   # to the function before the interrupt
   addi s0, sp, (10 * RISCV_SZPTR)
   mv a0, sp

   csrr t0, scause
   blt t0, zero, .handle_irq

   REG_S t0, 36 * RISCV_SZPTR(sp)   # save int_num

   li t1, EXC_SYSCALL
   beq t1, t0, .handle_syscall

   tail fault_entry

.handle_syscall:
   tail syscall_entry

.handle_irq:
   li t1, 1 << (__riscv_xlen - 1)
   not t1, t1
   and t0, t0, t1
   addi t0, t0, 32
   REG_S t0, 36 * RISCV_SZPTR(sp)   # save int_num

   tail irq_entry

asm_trap_entry_resume:
   csrci sstatus, SR_SIE
   REG_L t0, 33 * RISCV_SZPTR(sp)   # load sstatus
   andi t0, t0, SR_SPP
   bnez t0, .resume_kernel

.resume_user:
   REG_L t1, 38 * RISCV_SZPTR(sp)
   REG_S t1,  2 * RISCV_SZPTR(sp)   # restore usersp

   addi t0, sp, SIZEOF_REGS
   csrw sscratch, t0                # save kernel sp in sscratch

.resume_kernel:
   resume_all_regs
   sret

END_FUNC(asm_trap_entry)

FUNC(context_switch):

   mv sp, a0  # Make SP = function's 1st (and only) argument: regs *contex.
   REG_L ra, 37 * RISCV_SZPTR(sp)
   ret        # Now ra's value is `kernel_resume_pc`.
              # By default, that value is `asm_trap_entry_resume` or
              # 'kernel_yield_resume'

END_FUNC(context_switch)

