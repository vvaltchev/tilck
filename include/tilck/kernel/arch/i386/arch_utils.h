/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#if defined(__TILCK_KERNEL__) && !defined(__TILCK_HAL__)
#error Never include this header directly. Do #include <tilck/kernel/hal.h>.
#endif

#include <tilck/common/arch/generic_x86/x86_utils.h>
#include <tilck/kernel/arch/i386/asm_defs.h>

typedef struct regs regs;

struct regs {
   u32 kernel_resume_eip;
   u32 custom_flags;        /* custom Tilck flags */
   u32 gs, fs, es, ds;
   u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by 'pusha' */
   u32 int_num, err_code;    /* our 'push byte #' and error codes do this */
   u32 eip, cs, eflags, useresp, ss;   /* pushed by the CPU automatically */
};

STATIC_ASSERT(SIZEOF_REGS == sizeof(regs));
STATIC_ASSERT(REGS_EIP_OFF == OFFSET_OF(regs, eip));
STATIC_ASSERT(REGS_USERESP_OFF == OFFSET_OF(regs, useresp));

typedef struct {

   void *ldt;
   int ldt_size; /* Number of entries. Valid only if ldt != NULL. */
   int ldt_index_in_gdt; /* Index in gdt, valid only if ldt != NULL. */

   u16 gdt_entries[3]; /* Array of indexes in gdt, valid if > 0 */

   void *fpu_regs;
   size_t fpu_regs_size;

} arch_task_info_members;

static ALWAYS_INLINE int regs_intnum(regs *r)
{
   return (int)r->int_num;
}

static ALWAYS_INLINE void set_return_register(regs *r, u32 value)
{
   r->eax = value;
}

static ALWAYS_INLINE uptr get_curr_stack_ptr(void)
{
   uptr esp;

   asmVolatile("mov %%esp, %0"
               : "=r" (esp)
               : /* no input */
               : /* no clobber */);

   return esp;
}

NORETURN void context_switch(regs *r);

