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

   sret

END_FUNC(asm_trap_entry)


FUNC(context_switch):

   ret

END_FUNC(context_switch)
