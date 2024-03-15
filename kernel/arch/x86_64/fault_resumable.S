# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1

#include <tilck_gen_headers/config_global.h>
#include <tilck/kernel/arch/i386/asm_defs.h>

.code64

.section .text

.global fault_resumable_call

FUNC(fault_resumable_call):
                                   // our 1st arg (mask) -> ignored
   mov     rax, rsi                // our 2nd arg (func pointer) -> RAX
                                   // our 3rd arg (nargs) -> ignored
   mov     rdi, rcx                // our 4rd arg -> 1st arg for func (RDI)
   mov     rsi, r8                 // our 5th arg -> 2nd arg for func (RSI)
   mov     rdx, r9                 // our 6th arg -> 3rd arg for func (RDX)
   mov     rcx, QWORD PTR [rsp+16] // our 7th arg -> 4th arg of func (RCX)
   mov     r8, QWORD PTR [rsp+24]  // our 8th arg -> 5th arg of func (R8)
   mov     r9, QWORD PTR [rsp+32]  // our 9th arg -> 6th arg of func (R9)
   call    rax
   xor     rax, rax // simulate no faults, return 0
   ret

.asm_fault_resumable_call_resume:

   //TODO: implement this
   ret

END_FUNC(fault_resumable_call)

# Tell GNU ld to not worry about us having an executable stack
.section .note.GNU-stack,"",@progbits
