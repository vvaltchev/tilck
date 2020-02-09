# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1

#include <tilck/common/config.h>
#include <tilck/kernel/arch/i386/asm_defs.h>
#include <multiboot.h>

.code32

.section .text

.global asm_enable_sse
.global asm_enable_osxsave
.global asm_enable_avx
.global __asm_fpu_cpy_single_256_nt
.global __asm_fpu_cpy_single_256_nt_read

# This function initialize both the x87 FPU and SSE
FUNC(asm_enable_sse):

   mov eax, cr0
   and eax, ~CR0_EM  # set CR0.EM = 0 [coprocessor emulation]
   and eax, ~CR0_TS  # set CR0.TS = 0 [Task switched]
   or eax,  CR0_MP   # set CR0.MP = 1 [coprocessor monitoring]
   or eax,  CR0_NE   # set CR0.NE = 1 [Native Exception handling]
   mov cr0, eax
   fninit

   mov eax, cr4
   or eax, CR4_OSFXSR
   or eax, CR4_OSXMMEXCPT
   mov cr4, eax

   ret

END_FUNC(asm_enable_sse)

FUNC(asm_enable_osxsave):

   mov eax, cr4
   or eax, CR4_OSXSAVE
   mov cr4, eax

   ret

END_FUNC(asm_enable_osxsave)

FUNC(asm_enable_avx):

   xor ecx, ecx
   xgetbv            # Load XCR0 in eax
   or eax, XCR0_X87  # FPU/MMX x87 enabled [must be 1]
   or eax, XCR0_SSE  # Set SSE enabled     [can be 0 if AVX is 0]
   or eax, XCR0_AVX  # Set AVX enabled     [must be 1, if SSE is 1]
   xsetbv            # Save eax back to XCR0
   ret

END_FUNC(asm_enable_avx)

FUNC(__asm_fpu_cpy_single_256_nt):
   jmp memcpy_single_256_failsafe
   .space 128
END_FUNC(__asm_fpu_cpy_single_256_nt)

FUNC(__asm_fpu_cpy_single_256_nt_read):
   jmp memcpy_single_256_failsafe
   .space 128
END_FUNC(__asm_fpu_cpy_single_256_nt_read)
