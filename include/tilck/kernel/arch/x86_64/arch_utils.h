/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#if defined(__TILCK_KERNEL__) && !defined(__TILCK_HAL__)
   #error Never include this header directly. Do #include <tilck/kernel/hal.h>.
#endif

#include <tilck_gen_headers/config_kernel.h>
#include <tilck/kernel/arch/x86_64/asm_defs.h>

struct x86_64_regs {
   /* STUB struct */
   s32 int_num;
   u32 err_code;
   u64 rsp, rbp, rax, rip;
   u32 custom_flags;
};

struct x86_64_arch_proc_members {
   /* STUB struct */
   ulong some_var; /* avoid error: empty struct has size 0 in C, 1 in C++ */
};

struct x86_64_arch_task_members {
   u16 fpu_regs_size;
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

static ALWAYS_INLINE void *regs_get_stack_ptr(regs_t *r)
{
   return TO_PTR(r->rsp);
}

static ALWAYS_INLINE void *regs_get_frame_ptr(regs_t *r)
{
   return TO_PTR(r->rbp);
}

static ALWAYS_INLINE void *regs_get_ip(regs_t *r)
{
   return TO_PTR(r->rip);
}
