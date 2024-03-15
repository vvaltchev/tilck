# SPDX-License-Identifier: BSD-2-Clause
.intel_syntax noprefix

.code32
.text

.global is_stack_aligned_16
.global execute_illegal_instruction

# This function checks if ESP was aligned at 16-bytes boundary *before* the
# function was called. That's why we add +4 in the 2nd instruction: because
# the CALL instruction pushes the EIP on the stack and that makes it unaligned
# again.
is_stack_aligned_16:
   mov eax, esp
   add eax, 4
   and eax, 0xf
   cmp eax, 0
   je .ok
   mov eax, 0
   jmp .out
.ok:
   mov eax, 1
.out:
   ret

execute_illegal_instruction:
   .byte 0x0f
   .byte 0x0b
   ret # We should never get here


# Tell GNU ld to not worry about us having an executable stack
.section .note.GNU-stack,"",@progbits
