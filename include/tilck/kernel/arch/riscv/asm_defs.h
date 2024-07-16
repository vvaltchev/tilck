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
   #define TI_F_RESUME_RS_OFF     32 /* offset of: fault_resume_regs */
   #define TI_FAULTS_MASK_OFF     40 /* offset of: faults_resume_mask */
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

#endif
