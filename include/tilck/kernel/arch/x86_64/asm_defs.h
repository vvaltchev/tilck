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


#define REGS_FL_FPU_ENABLED     8


/* Some useful asm macros */
#ifdef ASM_FILE

#define NBITS           64

#define FUNC(x) .type x, @function; x
#define END_FUNC(x) .size x, .-(x)

#endif
