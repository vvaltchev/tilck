# SPDX-License-Identifier: BSD-2-Clause

#define ASM_FILE 1

#include <tilck_gen_headers/config_global.h>
#include <tilck_gen_headers/config_kernel.h>
#include <tilck_gen_headers/config_debug.h>

#include <tilck/kernel/arch/riscv/asm_defs.h>

.section .text

.global asm_save_fpu
.global asm_restore_fpu

FUNC(asm_save_fpu):

   li t1, SR_FS
   csrs sstatus, t1
   frcsr t0
   fsd f0,  0 * 8(a0)
   fsd f1,  1 * 8(a0)
   fsd f2,  2 * 8(a0)
   fsd f3,  3 * 8(a0)
   fsd f4,  4 * 8(a0)
   fsd f5,  5 * 8(a0)
   fsd f6,  6 * 8(a0)
   fsd f7,  7 * 8(a0)
   fsd f8,  8 * 8(a0)
   fsd f9,  9 * 8(a0)
   fsd f10, 10 * 8(a0)
   fsd f11, 11 * 8(a0)
   fsd f12, 12 * 8(a0)
   fsd f13, 13 * 8(a0)
   fsd f14, 14 * 8(a0)
   fsd f15, 15 * 8(a0)
   fsd f16, 16 * 8(a0)
   fsd f17, 17 * 8(a0)
   fsd f18, 18 * 8(a0)
   fsd f19, 19 * 8(a0)
   fsd f20, 20 * 8(a0)
   fsd f21, 21 * 8(a0)
   fsd f22, 22 * 8(a0)
   fsd f23, 23 * 8(a0)
   fsd f24, 24 * 8(a0)
   fsd f25, 25 * 8(a0)
   fsd f26, 26 * 8(a0)
   fsd f27, 27 * 8(a0)
   fsd f28, 28 * 8(a0)
   fsd f29, 29 * 8(a0)
   fsd f30, 30 * 8(a0)
   fsd f31, 31 * 8(a0)
   sw t0,   32 * 8(a0)
   csrc sstatus, t1
   ret

END_FUNC(asm_save_fpu)

FUNC(asm_restore_fpu):

   li t1, SR_FS
   lw t0, 32 * 8(a0)
   csrs sstatus, t1
   fld f0,  0 * 8(a0)
   fld f1,  1 * 8(a0)
   fld f2,  2 * 8(a0)
   fld f3,  3 * 8(a0)
   fld f4,  4 * 8(a0)
   fld f5,  5 * 8(a0)
   fld f6,  6 * 8(a0)
   fld f7,  7 * 8(a0)
   fld f8,  8 * 8(a0)
   fld f9,  9 * 8(a0)
   fld f10, 10 * 8(a0)
   fld f11, 11 * 8(a0)
   fld f12, 12 * 8(a0)
   fld f13, 13 * 8(a0)
   fld f14, 14 * 8(a0)
   fld f15, 15 * 8(a0)
   fld f16, 16 * 8(a0)
   fld f17, 17 * 8(a0)
   fld f18, 18 * 8(a0)
   fld f19, 19 * 8(a0)
   fld f20, 20 * 8(a0)
   fld f21, 21 * 8(a0)
   fld f22, 22 * 8(a0)
   fld f23, 23 * 8(a0)
   fld f24, 24 * 8(a0)
   fld f25, 25 * 8(a0)
   fld f26, 26 * 8(a0)
   fld f27, 27 * 8(a0)
   fld f28, 28 * 8(a0)
   fld f29, 29 * 8(a0)
   fld f30, 30 * 8(a0)
   fld f31, 31 * 8(a0)
   fscsr t0
   csrc sstatus, t1
   ret

END_FUNC(asm_restore_fpu)

