/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#if defined(__TILCK_KERNEL__) && !defined(__TILCK_HAL__)
   #ifndef CLANGD
      #error Never include this header directly. #include <tilck/kernel/hal.h>.
   #else
      #define get_stack_ptr()  ((ulong)0) // CLANGD-ONLY!
   #endif
#endif

#include <tilck_gen_headers/config_kernel.h>
#include <tilck/common/basic_defs.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>
#include <tilck/common/page_size.h>
#include <tilck/kernel/arch/i386/asm_defs.h>


struct x86_regs {
   u32 kernel_resume_eip;
   u32 custom_flags;        /* custom Tilck flags */
   u32 gs, fs, es, ds;
   u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by 'pusha' */
   s32 int_num;      /* our 'push byte #' do this */
   u32 err_code;     /* pushed by CPU in some cases, by us in others */
   u32 eip, cs, eflags, useresp, ss;   /* pushed by the CPU automatically */
};

typedef struct x86_regs regs_t;

STATIC_ASSERT(SIZEOF_REGS == sizeof(regs_t));
STATIC_ASSERT(REGS_EIP_OFF == OFFSET_OF(regs_t, eip));
STATIC_ASSERT(REGS_USERESP_OFF == OFFSET_OF(regs_t, useresp));

STATIC_ASSERT(X86_KERNEL_CODE_SEL == X86_SELECTOR(1, TABLE_GDT, 0));
STATIC_ASSERT(X86_KERNEL_DATA_SEL == X86_SELECTOR(2, TABLE_GDT, 0));
STATIC_ASSERT(X86_USER_CODE_SEL == X86_SELECTOR(3, TABLE_GDT, 3));
STATIC_ASSERT(X86_USER_DATA_SEL == X86_SELECTOR(4, TABLE_GDT, 3));

struct x86_arch_proc_members {
   void *ldt;
   u16 ldt_size; /* Number of entries. Valid only if ldt != NULL. */
   u16 ldt_index_in_gdt; /* Index in gdt, valid only if ldt != NULL. */
   u16 gdt_entries[3]; /* Array of indexes in gdt, valid if > 0 */
};

struct x86_arch_task_members {
   u16 fpu_regs_size;
   void *fpu_regs;
};

NORETURN void context_switch(regs_t *r);

static ALWAYS_INLINE int regs_intnum(regs_t *r)
{
   return r->int_num;
}

static ALWAYS_INLINE void set_return_register(regs_t *r, ulong value)
{
   r->eax = value;
}

static ALWAYS_INLINE ulong get_return_register(regs_t *r)
{
   return r->eax;
}

static ALWAYS_INLINE ulong get_rem_stack(void)
{
   return (get_stack_ptr() & ((ulong)KERNEL_STACK_SIZE - 1));
}

static ALWAYS_INLINE void *regs_get_stack_ptr(regs_t *r)
{
   return TO_PTR(r->esp);
}

static ALWAYS_INLINE void *regs_get_frame_ptr(regs_t *r)
{
   return TO_PTR(r->ebp);
}

static ALWAYS_INLINE void *regs_get_ip(regs_t *r)
{
   return TO_PTR(r->eip);
}

static ALWAYS_INLINE void regs_set_ip(regs_t *r, ulong value)
{
   r->eip = value;
}

static ALWAYS_INLINE ulong regs_get_usersp(regs_t *r)
{
   return r->useresp;
}

static ALWAYS_INLINE void regs_set_usersp(regs_t *r, ulong value)
{
   r->useresp = value;
}
