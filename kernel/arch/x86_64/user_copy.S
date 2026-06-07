# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1

#include <tilck_gen_headers/config_global.h>
#include <tilck/kernel/arch/i386/asm_defs.h>

.code64

.section .text

.global arch_user_copy
.global asm_user_copy_fault

#
# int arch_user_copy(void *dest, const void *src, size_t n)
#
# SysV AMD64: rdi = dest, rsi = src, rdx = n; returns in rax. See the i386
# version for how copy_{to,from}_user() arm asm_user_copy_fault around this
# call. Returns 0 if the whole copy succeeded, 1 if a fault was caught.
#

FUNC(arch_user_copy):

   mov rcx, rdx         # rcx = n   (RDI/RSI are already dest/src for movs)
   mov r8, rcx          # r8 = n    (saved for the byte tail)
   shr rcx, 3           # rcx = n / 8
   rep movsq            # copy qwords (may fault -> asm_user_copy_fault)
   mov rcx, r8
   and rcx, 7           # rcx = n % 8
   rep movsb            # copy tail bytes (may fault -> asm_user_copy_fault)

   xor eax, eax         # return 0: the whole copy succeeded
   ret

asm_user_copy_fault:

   # Reached only via the fault handler redirecting RIP here. No callee-saved
   # register was touched and RSP is unchanged, so we just return.

   mov eax, 1           # return 1: a user page fault was caught mid-copy
   ret

END_FUNC(arch_user_copy)

# Tell GNU ld to not worry about us having an executable stack
.section .note.GNU-stack,"",@progbits
