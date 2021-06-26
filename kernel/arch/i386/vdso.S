# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1
#include <tilck_gen_headers/config_mm.h>
#include <tilck/kernel/arch/i386/asm_defs.h>

.code32
.text

.global vdso_begin
.global vsdo_end
.global sysexit_user_code_user_vaddr

.align 4096
vdso_begin:

.align 4
# Sysexit will jump to here when returning to usermode and will
# do EXACTLY what the Linux kernel does in VSDO after sysexit.
.sysexit_user_code:
pop ebp
pop edx
pop ecx
ret

.space 4096-(.-vdso_begin), 0
vdso_end:

sysexit_user_code_user_vaddr:
.long USER_VDSO_VADDR + (offset .sysexit_user_code - vdso_begin)
