/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#if defined(__TILCK_KERNEL__) && !defined(__TILCK_HAL__)
#error Never include this header directly. Do #include <tilck/kernel/hal.h>.
#endif

#include <tilck/common/arch/generic_x86/x86_utils.h>
#include <tilck/kernel/arch/i386/asm_defs.h>

struct regs {
   u32 kernel_resume_eip;
   u32 custom_flags;        /* custom Tilck flags */
   u32 gs, fs, es, ds;
   u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by 'pusha' */
   s32 int_num;      /* our 'push byte #' do this */
   u32 err_code;     /* pushed by CPU in some cases, by us in others */
   u32 eip, cs, eflags, useresp, ss;   /* pushed by the CPU automatically */
};

STATIC_ASSERT(SIZEOF_REGS == sizeof(struct regs));
STATIC_ASSERT(REGS_EIP_OFF == OFFSET_OF(struct regs, eip));
STATIC_ASSERT(REGS_USERESP_OFF == OFFSET_OF(struct regs, useresp));

typedef struct {

   void *ldt;
   u16 ldt_size; /* Number of entries. Valid only if ldt != NULL. */
   u16 ldt_index_in_gdt; /* Index in gdt, valid only if ldt != NULL. */
   u16 gdt_entries[3]; /* Array of indexes in gdt, valid if > 0 */
   u16 fpu_regs_size;
   void *aligned_fpu_regs;

} arch_task_members;

static ALWAYS_INLINE int regs_intnum(struct regs *r)
{
   return r->int_num;
}

static ALWAYS_INLINE void set_return_register(struct regs *r, uptr value)
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

NORETURN void context_switch(struct regs *r);

