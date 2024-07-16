/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#if !defined(__riscv)
   #error This header can be used only for riscv architectures.
#endif

#include <tilck/common/basic_defs.h>
#include <tilck/common/arch/riscv/asm_consts.h>
#include <tilck/common/page_size.h>

/* Defines useful when calling fault_resumable_call() */
#define ALL_FAULTS_MASK (0xFFFFFFFF)
#define PAGE_FAULT_MASK    ((1 << EXC_INST_PAGE_FAULT) \
                         | (1 << EXC_LOAD_PAGE_FAULT) \
                         | (1 << EXC_STORE_PAGE_FAULT))

#define X86_PC_TIMER_IRQ           0
#define X86_PC_KEYBOARD_IRQ        1
#define X86_PC_COM2_COM4_IRQ       3
#define X86_PC_COM1_COM3_IRQ       4
#define X86_PC_SOUND_IRQ           5
#define X86_PC_FLOPPY_IRQ          6
#define X86_PC_LPT1_OR_SLAVE_IRQ   7
#define X86_PC_RTC_IRQ             8
#define X86_PC_ACPI_IRQ            9
#define X86_PC_PCI1_IRQ           10
#define X86_PC_PCI2_IRQ           11
#define X86_PC_PS2_MOUSE_IRQ      12
#define X86_PC_MATH_COPROC_IRQ    13
#define X86_PC_HD_IRQ             14

#define COM1 0x3f8
#define COM2 0x2f8
#define COM3 0x3e8
#define COM4 0x2e8

#define __STR(csr) #csr

#define csr_swap(csr, val)                    \
({                                            \
   unsigned long __v = (unsigned long)(val);  \
   asmVolatile("csrrw %0, " __STR(csr) ", %1" \
               : "=r" (__v) : "rK" (__v)      \
               : "memory");                   \
   __v;                                       \
})

#define csr_read(csr)                         \
({                                            \
   volatile unsigned long __v;                \
   asmVolatile("csrr %0, " __STR(csr)         \
               : "=r" (__v) :                 \
               : "memory");                   \
   __v;                                       \
})

#define csr_write(csr, val)                   \
({                                            \
   unsigned long __v = (unsigned long)(val);  \
   asmVolatile("csrw " __STR(csr) ", %0"      \
               : : "rK" (__v)                 \
               : "memory");                   \
})

#define csr_read_set(csr, val)                \
({                                            \
   unsigned long __v = (unsigned long)(val);  \
   asmVolatile("csrrs %0, " __STR(csr) ", %1" \
               : "=r" (__v) : "rK" (__v)      \
               : "memory");                   \
   __v;                                       \
})

#define csr_set(csr, val)                     \
({                                            \
   unsigned long __v = (unsigned long)(val);  \
   asmVolatile("csrs " __STR(csr) ", %0"      \
               : : "rK" (__v)                 \
               : "memory");                   \
})

#define csr_read_clear(csr, val)              \
({                        \
   unsigned long __v = (unsigned long)(val);  \
   asmVolatile("csrrc %0, " __STR(csr) ", %1" \
               : "=r" (__v) : "rK" (__v)      \
               : "memory");                   \
   __v;                                       \
})

#define csr_clear(csr, val)                   \
({                                            \
   unsigned long __v = (unsigned long)(val);  \
   asmVolatile("csrc " __STR(csr) ", %0"      \
               : : "rK" (__v)                 \
               : "memory");                   \
})


static ALWAYS_INLINE bool are_interrupts_enabled(void)
{
   return !!(csr_read(CSR_SSTATUS) & SR_SIE);
}

static ALWAYS_INLINE void disable_interrupts(ulong *var)
{
   *var = csr_read(CSR_SSTATUS);

   if (*var & SR_SIE) {
      csr_clear(CSR_SSTATUS, SR_SIE);
   }
}

static ALWAYS_INLINE void enable_interrupts(ulong *var)
{
   if (*var & SR_SIE) {
      csr_set(CSR_SSTATUS, SR_SIE);
   }
}

static ALWAYS_INLINE void disable_interrupts_forced(void)
{
   csr_clear(CSR_SSTATUS, SR_SIE);
}

static ALWAYS_INLINE void enable_interrupts_forced(void)
{
   csr_set(CSR_SSTATUS, SR_SIE);
}

static ALWAYS_INLINE void hw_fpu_enable(void)
{
   csr_set(CSR_SSTATUS, SR_FS_CLEAN);
}

static ALWAYS_INLINE void hw_fpu_disable(void)
{
   csr_clear(CSR_SSTATUS, SR_FS);
}

static ALWAYS_INLINE bool hw_is_fpu_enabled(void)
{
   return !!(csr_read(CSR_SSTATUS) & SR_FS);
}

static ALWAYS_INLINE void __set_curr_pdir(ulong paddr)
{
   csr_write(CSR_SATP, (paddr >> PAGE_SHIFT) | SATP_MODE);
   asmVolatile("sfence.vma" : : : "memory");
}

static ALWAYS_INLINE ulong __get_curr_pdir()
{
   return (csr_read(CSR_SATP) & SATP_PPN) << PAGE_SHIFT;
}

/*
 * Invalidates the TLB entry used for resolving the page containing 'vaddr'.
 */
static ALWAYS_INLINE void invalidate_page_hw(ulong vaddr)
{
   asmVolatile("sfence.vma %0" : : "r" (vaddr) : "memory");
}

static ALWAYS_INLINE void flush_icache_all(void)
{
   asmVolatile("fence.i" : : : "memory");
}

static ALWAYS_INLINE void halt(void)
{
   asmVolatile("wfi" : : : "memory");
}

static ALWAYS_INLINE bool in_hypervisor(void)
{
   //TODO
   return false;
}

static ALWAYS_INLINE u64 rdtime(void)
{
#if __riscv_xlen == 64
   u64 n;
   asmVolatile(
      "rdtime %0"
      : "=r" (n));
   return n;
#else
   u32 lo, hi, tmp;
   asmVolatile(
      "1:\n"
      "rdtimeh %0\n"
      "rdtime %1\n"
      "rdtimeh %2\n"
      "bne %0, %2, 1b"
      : "=&r" (hi), "=&r" (lo), "=&r" (tmp));
   return ((u64)hi << 32) | lo;
#endif
}

static ALWAYS_INLINE u64 __rdtsc(void)
{
#if __riscv_xlen == 64
   u64 n;
   asmVolatile(
      "rdcycle %0"
      : "=r" (n));
   return n;
#else
   u32 lo, hi, tmp;
   asmVolatile(
      "1:\n"
      "rdcycleh %0\n"
      "rdcycle %1\n"
      "rdcycleh %2\n"
      "bne %0, %2, 1b"
      : "=&r" (hi), "=&r" (lo), "=&r" (tmp));
   return ((u64)hi << 32) | lo;
#endif
}

#define RDTSC() __rdtsc()
