/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck_gen_headers/config_global.h>
#include <tilck/common/arch/riscv/asm_consts.h>

#if KERNEL_STACK_PAGES == 1
   #define ASM_KERNEL_STACK_SZ      4096
#elif KERNEL_STACK_PAGES == 2
   #define ASM_KERNEL_STACK_SZ      8192
#elif KERNEL_STACK_PAGES == 4
   #define ASM_KERNEL_STACK_SZ      16384
#else
   #error Unsupported value of KERNEL_STACK_PAGES
#endif
#if __riscv_xlen == 64
   #define TI_F_RESUME_RS_OFF     40 /* offset of: fault_resume_regs */
   #define TI_FAULTS_MASK_OFF     48 /* offset of: faults_resume_mask */
#else
   #define TI_F_RESUME_RS_OFF     20 /* TODO: riscv32 */
   #define TI_FAULTS_MASK_OFF     24 /* TODO: riscv32 */
#endif

#if __riscv_xlen == 64
   #define SIZEOF_REGS     320
   #define RISCV_SZPTR     8
   #define RISCV_LOGSZPTR  3
#else
   #define SIZEOF_REGS     160
   #define RISCV_SZPTR     4
   #define RISCV_LOGSZPTR  2
#endif
/* Some useful asm macros */
#ifdef ASM_FILE

#if __riscv_xlen == 64
   #define RISCV_PTR       .dword
#else
   #define RISCV_PTR       .word
#endif

#if __riscv_xlen == 64
   #define REG_S    sd
   #define REG_L    ld
#else
   #define REG_S    sw
   #define REG_L    lw
#endif

#define NBITS           __riscv_xlen

#define FUNC(x) .type x, @function; x
#define END_FUNC(x) .size x, .-(x)

.macro save_fp_ra
   addi sp, sp, -(RISCV_SZPTR * 4)
   REG_S ra, 3 * RISCV_SZPTR(sp)
   REG_S s0, 2 * RISCV_SZPTR(sp)
   addi s0, sp, (RISCV_SZPTR * 4)
.endm

.macro restore_fp_ra
   REG_L ra, 3 * RISCV_SZPTR(sp)
   REG_L s0, 2 * RISCV_SZPTR(sp)
   addi  sp, sp, (RISCV_SZPTR * 4)
.endm

.macro save_callee_regs

   mv t0, sp
   addi sp, sp, -SIZEOF_REGS

   REG_S ra,   1 * RISCV_SZPTR(sp)
   REG_S t0,   2 * RISCV_SZPTR(sp)
   REG_S s0,   8 * RISCV_SZPTR(sp)
   REG_S s1,   9 * RISCV_SZPTR(sp)
   REG_S s2,  18 * RISCV_SZPTR(sp)
   REG_S s3,  19 * RISCV_SZPTR(sp)
   REG_S s4,  20 * RISCV_SZPTR(sp)
   REG_S s5,  21 * RISCV_SZPTR(sp)
   REG_S s6,  22 * RISCV_SZPTR(sp)
   REG_S s7,  23 * RISCV_SZPTR(sp)
   REG_S s8,  24 * RISCV_SZPTR(sp)
   REG_S s9,  25 * RISCV_SZPTR(sp)
   REG_S s10, 26 * RISCV_SZPTR(sp)
   REG_S s11, 27 * RISCV_SZPTR(sp)

   csrr s1, sstatus
   REG_S s1, 33 * RISCV_SZPTR(sp)

.endm

.macro resume_callee_regs

   REG_L s1, 33 * RISCV_SZPTR(sp)
   csrw sstatus, s1

   REG_L ra,   1 * RISCV_SZPTR(sp)
   REG_L t0,   2 * RISCV_SZPTR(sp)
   REG_L s0,   8 * RISCV_SZPTR(sp)
   REG_L s1,   9 * RISCV_SZPTR(sp)
   REG_L s2,  18 * RISCV_SZPTR(sp)
   REG_L s3,  19 * RISCV_SZPTR(sp)
   REG_L s4,  20 * RISCV_SZPTR(sp)
   REG_L s5,  21 * RISCV_SZPTR(sp)
   REG_L s6,  22 * RISCV_SZPTR(sp)
   REG_L s7,  23 * RISCV_SZPTR(sp)
   REG_L s8,  24 * RISCV_SZPTR(sp)
   REG_L s9,  25 * RISCV_SZPTR(sp)
   REG_L s10, 26 * RISCV_SZPTR(sp)
   REG_L s11, 27 * RISCV_SZPTR(sp)

   REG_L sp,   2 * RISCV_SZPTR(sp)

.endm

.macro save_all_regs

   addi sp, sp, -SIZEOF_REGS

   REG_S x1,   1 * RISCV_SZPTR(sp)

   REG_S x3,   3 * RISCV_SZPTR(sp)
   REG_S x4,   4 * RISCV_SZPTR(sp)
   REG_S x5,   5 * RISCV_SZPTR(sp)
   REG_S x6,   6 * RISCV_SZPTR(sp)
   REG_S x7,   7 * RISCV_SZPTR(sp)
   REG_S x8,   8 * RISCV_SZPTR(sp)
   REG_S x9,   9 * RISCV_SZPTR(sp)
   REG_S x10, 10 * RISCV_SZPTR(sp)
   REG_S x11, 11 * RISCV_SZPTR(sp)
   REG_S x12, 12 * RISCV_SZPTR(sp)
   REG_S x13, 13 * RISCV_SZPTR(sp)
   REG_S x14, 14 * RISCV_SZPTR(sp)
   REG_S x15, 15 * RISCV_SZPTR(sp)
   REG_S x16, 16 * RISCV_SZPTR(sp)
   REG_S x17, 17 * RISCV_SZPTR(sp)
   REG_S x18, 18 * RISCV_SZPTR(sp)
   REG_S x19, 19 * RISCV_SZPTR(sp)
   REG_S x20, 20 * RISCV_SZPTR(sp)
   REG_S x21, 21 * RISCV_SZPTR(sp)
   REG_S x22, 22 * RISCV_SZPTR(sp)
   REG_S x23, 23 * RISCV_SZPTR(sp)
   REG_S x24, 24 * RISCV_SZPTR(sp)
   REG_S x25, 25 * RISCV_SZPTR(sp)
   REG_S x26, 26 * RISCV_SZPTR(sp)
   REG_S x27, 27 * RISCV_SZPTR(sp)
   REG_S x28, 28 * RISCV_SZPTR(sp)
   REG_S x29, 29 * RISCV_SZPTR(sp)
   REG_S x30, 30 * RISCV_SZPTR(sp)
   REG_S x31, 31 * RISCV_SZPTR(sp)

   csrr s0, sepc
   csrr s1, sstatus
   csrr s2, stval
   csrr s3, scause
   csrr s4, sscratch

   REG_S s0, 32 * RISCV_SZPTR(sp)
   REG_S s1, 33 * RISCV_SZPTR(sp)
   REG_S s2, 34 * RISCV_SZPTR(sp)
   REG_S s3, 35 * RISCV_SZPTR(sp)
   REG_S s4, 38 * RISCV_SZPTR(sp)

   addi s0, sp, SIZEOF_REGS
   REG_S s0,  2 * RISCV_SZPTR(sp)

.endm

.macro resume_all_regs

   REG_L s0, 32 * RISCV_SZPTR(sp)
   REG_L s1, 33 * RISCV_SZPTR(sp)
   andi s1, s1, ~SR_SIE //The interrupt must remain closed until sret
   csrw sepc, s0
   csrw sstatus, s1

   REG_L x1,   1 * RISCV_SZPTR(sp)

   REG_L x3,   3 * RISCV_SZPTR(sp)
   REG_L x4,   4 * RISCV_SZPTR(sp)
   REG_L x5,   5 * RISCV_SZPTR(sp)
   REG_L x6,   6 * RISCV_SZPTR(sp)
   REG_L x7,   7 * RISCV_SZPTR(sp)
   REG_L x8,   8 * RISCV_SZPTR(sp)
   REG_L x9,   9 * RISCV_SZPTR(sp)
   REG_L x10, 10 * RISCV_SZPTR(sp)
   REG_L x11, 11 * RISCV_SZPTR(sp)
   REG_L x12, 12 * RISCV_SZPTR(sp)
   REG_L x13, 13 * RISCV_SZPTR(sp)
   REG_L x14, 14 * RISCV_SZPTR(sp)
   REG_L x15, 15 * RISCV_SZPTR(sp)
   REG_L x16, 16 * RISCV_SZPTR(sp)
   REG_L x17, 17 * RISCV_SZPTR(sp)
   REG_L x18, 18 * RISCV_SZPTR(sp)
   REG_L x19, 19 * RISCV_SZPTR(sp)
   REG_L x20, 20 * RISCV_SZPTR(sp)
   REG_L x21, 21 * RISCV_SZPTR(sp)
   REG_L x22, 22 * RISCV_SZPTR(sp)
   REG_L x23, 23 * RISCV_SZPTR(sp)
   REG_L x24, 24 * RISCV_SZPTR(sp)
   REG_L x25, 25 * RISCV_SZPTR(sp)
   REG_L x26, 26 * RISCV_SZPTR(sp)
   REG_L x27, 27 * RISCV_SZPTR(sp)
   REG_L x28, 28 * RISCV_SZPTR(sp)
   REG_L x29, 29 * RISCV_SZPTR(sp)
   REG_L x30, 30 * RISCV_SZPTR(sp)
   REG_L x31, 31 * RISCV_SZPTR(sp)

   REG_L x2, 2 * RISCV_SZPTR(sp)
.endm

#endif

