# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1

#include <tilck_gen_headers/config_global.h>
#include <tilck/kernel/arch/i386/asm_defs.h>

.code32

.section .text

.global arch_user_copy
.global asm_user_copy_fault

#
# int arch_user_copy(void *dest, const void *src, size_t n)
#
# Copy `n` bytes from `src` to `dest`. Used by copy_{to,from}_user(): the C
# caller arms current->user_access_fixup with asm_user_copy_fault before the
# call and clears it after. If a `movs` below faults on a user page, the fault
# handler resumes execution at asm_user_copy_fault instead of panicking (see
# user_access_resume_on_fault() and handle_fault()). Returns 0 if the whole
# copy succeeded, 1 if a fault was caught mid-copy.
#

FUNC(arch_user_copy):

   push edi             # EDI and ESI are callee-saved in the i386 cdecl ABI
   push esi

   mov edi, [esp + 12]  # arg1: dest
   mov esi, [esp + 16]  # arg2: src
   mov ecx, [esp + 20]  # arg3: n

   mov edx, ecx
   shr ecx, 2           # ecx = n / 4
   rep movsd            # copy dwords (may fault -> asm_user_copy_fault)
   mov ecx, edx
   and ecx, 3           # ecx = n % 4
   rep movsb            # copy tail bytes (may fault -> asm_user_copy_fault)

   xor eax, eax         # return 0: the whole copy succeeded
   pop esi
   pop edi
   ret

asm_user_copy_fault:

   # Reached only via the fault handler redirecting EIP here. ESP is unchanged
   # from the faulting `movs` (the pushed EDI/ESI are still there), so we can
   # restore them and return exactly like the normal path.

   mov eax, 1           # return 1: a user page fault was caught mid-copy
   pop esi
   pop edi
   ret

END_FUNC(arch_user_copy)

# Tell GNU ld to not worry about us having an executable stack
.section .note.GNU-stack,"",@progbits
