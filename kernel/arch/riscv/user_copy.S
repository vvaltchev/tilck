# SPDX-License-Identifier: BSD-2-Clause

#define ASM_FILE 1

#include <tilck_gen_headers/config_global.h>
#include <tilck/kernel/arch/riscv/asm_defs.h>

.section .text

.global arch_user_copy
.global asm_user_copy_fault

#
# int arch_user_copy(void *dest, const void *src, size_t n)
#
# a0 = dest, a1 = src, a2 = n; returns in a0. See the i386 version for how
# copy_{to,from}_user() arm asm_user_copy_fault around this call. A plain byte
# loop: byte loads/stores never trap on misalignment, so the only fault that
# can occur is a user page fault. Returns 0 if the whole copy succeeded, 1 if
# a fault was caught mid-copy.
#

FUNC(arch_user_copy):

   beqz a2, 2f
1:
   lb   t0, 0(a1)       # load byte  (may fault -> asm_user_copy_fault)
   sb   t0, 0(a0)       # store byte (may fault -> asm_user_copy_fault)
   addi a0, a0, 1
   addi a1, a1, 1
   addi a2, a2, -1
   bnez a2, 1b
2:
   li   a0, 0           # return 0: the whole copy succeeded
   ret

asm_user_copy_fault:

   # Reached only via the fault handler redirecting sepc here. No callee-saved
   # register was touched and the stack is unchanged, so we just return.

   li   a0, 1           # return 1: a user page fault was caught mid-copy
   ret

END_FUNC(arch_user_copy)
