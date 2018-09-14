# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1

#include <tilck/common/config.h>
#include <tilck/kernel/arch/i386/asm_defs.h>

.code32

.section .text

.global fault_resumable_call

FUNC(fault_resumable_call):

   mov ecx, [__current]
   push [ecx + TI_F_RESUME_RS_OFF]   # push current->fault_resume_regs
   push [ecx + TI_FAULTS_MASK_OFF]   # push current->faults_resume_mask

   push ebp
   mov ebp, esp

   push [disable_preemption_count]
   sub esp, 8        # skip 'ss', 'esp'
   pushf
   sub esp, 16       # skip cs, eip, err_code and int_num
   pusha
   sub esp, 16       # skip pushing ds, es, fs, gs
   push offset .asm_fault_resumable_call_resume

   mov ecx, [__current]
   mov [ecx + TI_F_RESUME_RS_OFF], esp

   mov eax, [ebp + EBP_OFFSET_ARG1 + 8]  # arg1: faults_mask
   mov [ecx + TI_FAULTS_MASK_OFF], eax

   mov eax, [ebp + EBP_OFFSET_ARG2 + 8]  # arg2: func
   lea edx, [ebp + EBP_OFFSET_ARG3 + 8]  # &arg3: &nargs
   mov ecx, [edx]                        # arg3: nargs
   shl ecx, 2                            # nargs *= 4

   test ecx, ecx
   jz 2f
1:
   push [edx + ecx]
   sub ecx, 4
   jnz 1b
2:
   call eax
   xor eax, eax      # return value: set to 0 (= no faults)
   leave

   mov ecx, [__current]
   pop [ecx + TI_FAULTS_MASK_OFF]
   pop [ecx + TI_F_RESUME_RS_OFF]
   ret

.asm_fault_resumable_call_resume:

   add esp, 16
   popa
   add esp, 16
   popf
   add esp, 8
   pop [disable_preemption_count]
   leave

   mov ecx, [__current]
   pop [ecx + TI_FAULTS_MASK_OFF]
   pop [ecx + TI_F_RESUME_RS_OFF]
   ret

END_FUNC(fault_resumable_call)
