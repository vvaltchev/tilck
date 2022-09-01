/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck_gen_headers/config_global.h>
#include <tilck/common/arch/generic_x86/asm_consts.h>

#if KERNEL_STACK_PAGES == 1
   #define ASM_KERNEL_STACK_SZ      4096
#elif KERNEL_STACK_PAGES == 2
   #define ASM_KERNEL_STACK_SZ      8192
#elif KERNEL_STACK_PAGES == 4
   #define ASM_KERNEL_STACK_SZ      16384
#else
   #error Unsupported value of KERNEL_STACK_PAGES
#endif

#define TI_F_RESUME_RS_OFF     20 /* offset of: fault_resume_regs */
#define TI_FAULTS_MASK_OFF     24 /* offset of: faults_resume_mask */

#define SIZEOF_REGS            84
#define REGS_EIP_OFF           64
#define REGS_USERESP_OFF       76

#define REGS_FL_SYSENTER        1
#define REGS_FL_FPU_ENABLED     8

#define X86_KERNEL_CODE_SEL  0x08
#define X86_KERNEL_DATA_SEL  0x10
#define X86_USER_CODE_SEL    0x1b
#define X86_USER_DATA_SEL    0x23

/* Some useful asm macros */
#ifdef ASM_FILE

#define NBITS           32

#define FUNC(x) .type x, @function; x
#define END_FUNC(x) .size x, .-(x)

#define EBP_OFFSET_ARG1  8
#define EBP_OFFSET_ARG2 12
#define EBP_OFFSET_ARG3 16

.macro asm_disable_cr0_ts
   mov eax, CR0
   and eax, ~8
   mov CR0, eax
.endm

.macro push_custom_flags additional_flags
   mov eax, CR0
   and eax, 8       // CR0_TS
   xor eax, 8       // flip the bit:
                    // we use FPU_ENABLED while TS=1 means FPU disabled

   or eax, \additional_flags
   push eax         // custom_flags

   asm_disable_cr0_ts
.endm

.macro pop_custom_flags
   pop eax       // custom_flags
   and eax, 8    // REGS_FL_FPU_ENABLED
   xor eax, 8    // flip the bit
   mov ebx, CR0
   and ebx, ~8
   or ebx, eax
   mov CR0, ebx
.endm

.macro skip_push_custom_flags
   push 0
.endm

.macro skip_pop_custom_flags
   add esp, 4
.endm

.macro save_base_regs
   pusha             # Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
                     # Note: the value of the pushed ESP is the one before
                     # the pusha instruction.
   push ds
   push es
   push fs
   push gs
.endm

.macro resume_base_regs
   pop gs
   pop fs
   pop es
   pop ds
   popa
.endm

.macro kernel_entry_common

   save_base_regs

   mov ax, X86_KERNEL_DATA_SEL
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax

.endm

.macro kernel_exit_common

   resume_base_regs

   add esp, 8        // Discards the pushed err_code and int_num
   iret

.endm

#endif
