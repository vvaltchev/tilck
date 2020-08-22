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

#else

   #error Unsupported architecture.

#endif

STATIC_ASSERT(ARCH_TASK_MEMBERS_SIZE == sizeof(arch_task_members_t));
STATIC_ASSERT(ARCH_TASK_MEMBERS_ALIGN == alignof(arch_task_members_t));

STATIC_ASSERT(ARCH_PROC_MEMBERS_SIZE == sizeof(arch_proc_members_t));
STATIC_ASSERT(ARCH_PROC_MEMBERS_ALIGN == alignof(arch_proc_members_t));

void reboot();
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
void hw_read_clock(struct datetime *out);
u32 hw_timer_setup(u32 hz);

bool allocate_fpu_regs(arch_task_members_t *arch_fields);
void copy_main_tss_on_regs(regs_t *ctx);

#define get_task_arch_fields(ti) ((arch_task_members_t *)(ti)->arch_fields)
#define get_proc_arch_fields(pi) ((arch_proc_members_t *)(pi)->arch_fields)
