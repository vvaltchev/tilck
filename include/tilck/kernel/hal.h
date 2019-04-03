/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#define __TILCK_HAL__

#include <tilck/common/basic_defs.h>

#if defined(__i386__) && !defined(__x86_64__)

   #define __arch__x86__

   #include <tilck/common/arch/generic_x86/x86_utils.h>
   #include <tilck/common/arch/generic_x86/cpu_features.h>
   #include <tilck/kernel/arch/i386/asm_defs.h>
   #include <tilck/kernel/arch/i386/arch_utils.h>
   #include <tilck/kernel/arch/generic_x86/fpu_memcpy.h>

#elif defined(__x86_64__)

   #define __arch__x86__

   #include <tilck/common/arch/generic_x86/x86_utils.h>
   #include <tilck/common/arch/generic_x86/cpu_features.h>
   #include <tilck/kernel/arch/x86_64/arch_utils.h>
   #include <tilck/kernel/arch/generic_x86/fpu_memcpy.h>

#else

   #error Unsupported architecture.

#endif

enum irq_action {

   IRQ_UNHANDLED = -1,         /* the irq was not handled at all */
   IRQ_FULLY_HANDLED = 0,      /* no more work required */
   IRQ_REQUIRES_BH = 1         /* requires a botton half (tasklet) to run */
};

typedef void (*soft_int_handler_t)(regs *);
typedef enum irq_action (*irq_handler_t)(regs *);

void reboot();
void init_segmentation(void);
void init_cpu_exception_handling(void);
void init_syscall_interfaces(void);
void set_kernel_stack(uptr stack);
void enable_cpu_features(void);
void fpu_context_begin(void);
void fpu_context_end(void);
void save_current_fpu_regs(bool in_kernel);
void restore_fpu_regs(void *task, bool in_kernel);
void restore_current_fpu_regs(bool in_kernel);
int get_irq_num(regs *context);

bool allocate_fpu_regs(arch_task_info_members *arch_fields);
