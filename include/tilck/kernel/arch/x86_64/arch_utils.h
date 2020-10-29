/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#if defined(__TILCK_KERNEL__) && !defined(__TILCK_HAL__)
   #error Never include this header directly. Do #include <tilck/kernel/hal.h>.
#endif

#include <tilck_gen_headers/config_kernel.h>

struct x86_64_regs {
   /* STUB struct */
   ulong some_var; /* avoid error: empty struct has size 0 in C, 1 in C++ */
};

struct x86_64_arch_proc_members {
   /* STUB struct */
   ulong some_var; /* avoid error: empty struct has size 0 in C, 1 in C++ */
};

struct x86_64_arch_task_members {
   /* STUB struct */
   void *aligned_fpu_regs;
};

static ALWAYS_INLINE int regs_intnum(regs_t *r)
{
   NOT_IMPLEMENTED();
   return 0;
}

static ALWAYS_INLINE void set_return_register(regs_t *r, ulong value)
{
   NOT_IMPLEMENTED();
}

NORETURN static ALWAYS_INLINE void context_switch(regs_t *r)
{
   NOT_IMPLEMENTED();
}

static ALWAYS_INLINE ulong get_rem_stack(void)
{
   return (get_stack_ptr() & ((ulong)KERNEL_STACK_SIZE - 1));
}
