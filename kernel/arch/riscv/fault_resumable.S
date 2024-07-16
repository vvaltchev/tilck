# SPDX-License-Identifier: BSD-2-Clause

#define ASM_FILE 1

#include <tilck_gen_headers/config_global.h>
#include <tilck/kernel/arch/riscv/asm_defs.h>

.section .text

.global fault_resumable_call

FUNC(fault_resumable_call):

   ret

END_FUNC(fault_resumable_call)
