# SPDX-License-Identifier: BSD-2-Clause

.text

.global is_stack_aligned_16
.global execute_illegal_instruction

# This function checks if SP was aligned at 16-bytes boundary *before* the
# function was called.
is_stack_aligned_16:
   andi a0, sp, 0xf
   beqz a0, .ok
   li a0, 0
   j .out
.ok:
   li a0, 1
.out:
   ret

execute_illegal_instruction:
   .zero 4
   ret # We should never get here
