/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#if defined(__TILCK_KERNEL__) && !defined(__TILCK_HAL__)
   #error Never include this header directly. Do #include <tilck/kernel/hal.h>.
#endif

#include <tilck_gen_headers/config_kernel.h>
#include <tilck/common/arch/riscv/riscv_utils.h>
#include <tilck/kernel/arch/riscv/asm_defs.h>

struct riscv_regs {
   ulong zero;
   ulong ra;
   ulong sp;
   ulong gp;
   ulong tp;
   ulong t0;
   ulong t1;
   ulong t2;
   ulong s0;   /* frame pointer */
   ulong s1;
   ulong a0;
   ulong a1;
   ulong a2;
   ulong a3;
   ulong a4;
   ulong a5;
   ulong a6;
   ulong a7;
   ulong s2;
   ulong s3;
   ulong s4;
   ulong s5;
   ulong s6;
   ulong s7;
   ulong s8;
   ulong s9;
   ulong s10;
   ulong s11;
   ulong t3;
   ulong t4;
   ulong t5;
   ulong t6;
   ulong sepc;
   ulong sstatus;
   ulong sbadaddr;
   ulong scause;

   ulong int_num;
   ulong kernel_resume_pc;
   ulong usersp;
   ulong padding[1];
};

STATIC_ASSERT(SIZEOF_REGS == sizeof(regs_t));

struct riscv_arch_proc_members {
   void *none;
};

struct riscv_arch_task_members {
   u64 fpu_regs_size;
   void *fpu_regs;
};

NORETURN void context_switch(regs_t *r);

static ALWAYS_INLINE int regs_intnum(regs_t *r)
{
   int cause = (int)r->scause & 0xff;

   return cause + ((r->scause & CAUSE_IRQ_FLAG) ? 32 : 0);
}

static ALWAYS_INLINE void set_return_register(regs_t *r, ulong value)
{
   r->a0 = value;
}

static ALWAYS_INLINE ulong get_rem_stack(void)
{
   return (get_stack_ptr() & ((ulong)KERNEL_STACK_SIZE - 1));
}

static ALWAYS_INLINE void *regs_get_stack_ptr(regs_t *r)
{
   return TO_PTR(r->sp);
}

static ALWAYS_INLINE void *regs_get_frame_ptr(regs_t *r)
{
   return TO_PTR(r->s0);
}

static ALWAYS_INLINE void *regs_get_ip(regs_t *r)
{
   return TO_PTR(r->sepc);
}

static ALWAYS_INLINE u32 get_boothartid(void)
{
   extern u32 _boot_cpu_hartid;
   return _boot_cpu_hartid;
}

static ALWAYS_INLINE void *fdt_get_address(void)
{
   extern void * fdt_blob;
   return fdt_blob;
}
