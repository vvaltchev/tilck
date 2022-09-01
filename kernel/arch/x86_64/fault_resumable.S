# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1

#include <tilck_gen_headers/config_global.h>
#include <tilck/kernel/arch/i386/asm_defs.h>

.code64

.section .text

.global fault_resumable_call

FUNC(fault_resumable_call):

   # TODO: implement this
   ret

.asm_fault_resumable_call_resume:

   # TODO: implement this
   ret

END_FUNC(fault_resumable_call)
