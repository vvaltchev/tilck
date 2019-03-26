/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#if defined(__TILCK_KERNEL__) && !defined(__TILCK_HAL__)
#error Never include this header directly. Do #include <tilck/kernel/hal.h>.
#endif

struct regs {
   /* STUB struct */
   uptr some_var; /* avoid error: empty struct has size 0 in C, size 1 in C++ */
};

typedef struct regs regs;

typedef struct {

   /* STUB struct */
   void *fpu_regs;

} arch_task_info_members;

static ALWAYS_INLINE int regs_intnum(regs *r)
{
   NOT_IMPLEMENTED();
   return 0;
}

static ALWAYS_INLINE void set_return_register(regs *r, uptr value)
{
   NOT_IMPLEMENTED();
}

static ALWAYS_INLINE uptr get_curr_stack_ptr(void)
{
   NOT_IMPLEMENTED();
   return 0;
}

static ALWAYS_INLINE NORETURN void context_switch(regs *r)
{
   NOT_IMPLEMENTED();
}
