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

   ret

END_FUNC(asm_save_fpu)

FUNC(asm_restore_fpu):

   ret

END_FUNC(asm_restore_fpu)
