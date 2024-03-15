# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1
#include <tilck_gen_headers/config_mm.h>
#include <tilck/kernel/arch/i386/asm_defs.h>

.code32
.text

.global vdso_begin
.global vdso_end

.align 4096
vdso_begin:

.align 4
# Sysexit will jump to here when returning to usermode and will
# do EXACTLY what the Linux kernel does in VDSO after sysexit.
.sysexit_user_code:
pop ebp
pop edx
pop ecx
ret

.align 4
# When each signal handler returns, it will jump here
.post_sig_handler:
mov eax, 173 # sys_rt_sigreturn_impl()
int 0x80     # do the syscall

.align 4
# When we cannot immediately KILL a task != current, we make it call sys_pause()
# and it will be killed after the task switch, in handle_syscall().
.pause_trampoline:
mov eax, 29 # sys_pause()
int 0x80

.space 4096-(.-vdso_begin), 0
vdso_end:

.global sysexit_user_code_user_vaddr
sysexit_user_code_user_vaddr:
.long USER_VDSO_VADDR + (offset .sysexit_user_code - vdso_begin)

.global post_sig_handler_user_vaddr
post_sig_handler_user_vaddr:
.long USER_VDSO_VADDR + (offset .post_sig_handler - vdso_begin)

.global pause_trampoline_user_vaddr
pause_trampoline_user_vaddr:
.long USER_VDSO_VADDR + (offset .pause_trampoline - vdso_begin)

# Tell GNU ld to not worry about us having an executable stack
.section .note.GNU-stack,"",@progbits
