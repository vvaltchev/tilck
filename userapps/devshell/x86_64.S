# SPDX-License-Identifier: BSD-2-Clause
.intel_syntax noprefix

.code64
.text

.global is_stack_aligned_16
.global execute_illegal_instruction

# This function checks if RSP was aligned at 16-bytes boundary *before* the
# function was called. That's why we add +4 in the 2nd instruction: because
# the CALL instruction pushes the RIP on the stack and that makes it unaligned
# again.
is_stack_aligned_16:
   mov rax, rsp
   add rax, 4
   and rax, 0xf
   cmp rax, 0
   je .ok
   mov rax, 0
   jmp .out
.ok:
   mov rax, 1
.out:
   ret

execute_illegal_instruction:
   .byte 0x0f
   .byte 0x0b
   ret # We should never get here
