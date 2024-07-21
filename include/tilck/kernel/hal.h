/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#define __TILCK_HAL__

#include <tilck/common/basic_defs.h>
#include <tilck/common/datetime.h>
#include <tilck/kernel/hal_types.h>

#if defined(__i386__) || defined(__x86_64__)

   #define arch_x86_family

   #include <tilck/common/arch/generic_x86/x86_utils.h>
   #include <tilck/common/arch/generic_x86/cpu_features.h>
   #include <tilck/kernel/arch/generic_x86/fpu_memcpy.h>
   #include <tilck/kernel/arch/generic_x86/arch_ints.h>

   #if defined(__x86_64__)

      #include <tilck/common/arch/x86_64/utils.h>
      #include <tilck/kernel/arch/x86_64/arch_utils.h>

   #else

      #include <tilck/common/arch/i386/utils.h>
      #include <tilck/kernel/arch/i386/asm_defs.h>
      #include <tilck/kernel/arch/i386/arch_utils.h>
      #include <tilck/kernel/arch/i386/tss.h>

   #endif

#elif defined(__riscv)

   #include <tilck/common/arch/riscv/riscv_utils.h>
   #include <tilck/common/arch/riscv/utils.h>
   #include <tilck/common/arch/riscv/image.h>
   #include <tilck/kernel/arch/riscv/sbi.h>
   #include <tilck/kernel/arch/riscv/mmio.h>
   #include <tilck/kernel/arch/riscv/ioremap.h>
   #include <tilck/kernel/arch/riscv/arch_ints.h>
   #include <tilck/kernel/arch/riscv/asm_defs.h>
   #include <tilck/kernel/arch/riscv/arch_utils.h>
   #include <tilck/kernel/arch/riscv/fpu_memcpy.h>
   #include <tilck/kernel/arch/riscv/cpu_features.h>

   static ALWAYS_INLINE void init_segmentation(void)
   {
      /* STUB function: do nothing */
   }

#elif defined(__aarch64__) && defined(KERNEL_TEST)

   /*
    * Fake stub funcs and defines, just to allow the build of the kernel for
    * the unit tests
    */

   #define ALL_FAULTS_MASK (0xFFFFFFFF)
   #define PAGE_FAULT_MASK (1 << 14)
   #define SYSCALL_SOFT_INTERRUPT 0x80
   #define COM1 0
   #define COM2 1
   #define COM3 2
   #define COM4 3
   #define X86_PC_TIMER_IRQ 0

   static ALWAYS_INLINE bool are_interrupts_enabled(void)
   {
      return true;
   }

   static ALWAYS_INLINE void disable_interrupts(ulong *var)
   {
      /* STUB function: do nothing */
   }

   static ALWAYS_INLINE void enable_interrupts(ulong *var)
   {
      /* STUB function: do nothing */
   }

   static ALWAYS_INLINE void disable_interrupts_forced(void)
   {
      /* STUB function: do nothing */
   }

   static ALWAYS_INLINE void enable_interrupts_forced(void)
   {
      /* STUB function: do nothing */
   }

   static ALWAYS_INLINE void __set_curr_pdir(ulong paddr)
   {
      /* STUB function: do nothing */
   }

   static ALWAYS_INLINE ulong __get_curr_pdir()
   {
      NOT_IMPLEMENTED();
   }

   static ALWAYS_INLINE void set_return_register(regs_t *r, ulong value)
   {
      /* STUB function: do nothing */
   }

   static ALWAYS_INLINE int int_to_irq(int int_num)
   {
      NOT_IMPLEMENTED();
   }

   static ALWAYS_INLINE bool is_irq(int int_num)
   {
      NOT_IMPLEMENTED();
   }

   static ALWAYS_INLINE bool is_timer_irq(int int_num)
   {
      NOT_IMPLEMENTED();
   }

   static ALWAYS_INLINE bool is_fault(int int_num)
   {
      NOT_IMPLEMENTED();
   }

   static ALWAYS_INLINE int regs_intnum(regs_t *r)
   {
      NOT_IMPLEMENTED();
   }

   static ALWAYS_INLINE void halt(void)
   {
      /* STUB function: do nothing */
   }

   static ALWAYS_INLINE void init_fpu_memcpy(void)
   {
      /* STUB function: do nothing */
   }

   static ALWAYS_INLINE bool in_hypervisor(void)
   {
      return false;
   }

   static ALWAYS_INLINE ulong get_rem_stack(void)
   {
      return KERNEL_STACK_SIZE;
   }

   static ALWAYS_INLINE ulong get_stack_ptr(void)
   {
      return 0;
   }

   static ALWAYS_INLINE u64 RDTSC(void)
   {
      return 0;
   }

#else

   #error Unsupported architecture.

#endif

STATIC_ASSERT(ARCH_TASK_MEMBERS_SIZE == sizeof(arch_task_members_t));
STATIC_ASSERT(ARCH_TASK_MEMBERS_ALIGN == alignof(arch_task_members_t));

STATIC_ASSERT(ARCH_PROC_MEMBERS_SIZE == sizeof(arch_proc_members_t));
STATIC_ASSERT(ARCH_PROC_MEMBERS_ALIGN == alignof(arch_proc_members_t));

NORETURN void reboot(void);
NORETURN void poweroff(void);
void init_segmentation(void);
void init_cpu_exception_handling(void);
void init_syscall_interfaces(void);
void set_kernel_stack(ulong stack);
void enable_cpu_features(void);
void fpu_context_begin(void);
void fpu_context_end(void);
void save_current_fpu_regs(bool in_kernel);
void restore_fpu_regs(void *task, bool in_kernel);
void restore_current_fpu_regs(bool in_kernel);
int get_irq_num(regs_t *context);
int get_int_num(regs_t *context);
void on_first_pdir_update(void);
extern void (*hw_read_clock)(struct datetime *out);
void hw_read_clock_cmos(struct datetime *out);
u32 hw_timer_setup(u32 hz);

bool allocate_fpu_regs(arch_task_members_t *arch_fields);
void copy_main_tss_on_regs(regs_t *ctx);
void arch_add_initial_mem_regions();
bool arch_add_final_mem_regions();

#define get_task_arch_fields(ti) ((arch_task_members_t*)(void*)((ti)->ti_arch))
#define get_proc_arch_fields(pi) ((arch_proc_members_t*)(void*)((pi)->pi_arch))
