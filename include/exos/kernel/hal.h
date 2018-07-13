
#pragma once
#define __EXOS_HAL__

#include <exos/common/basic_defs.h>

#if defined(__i386__)

   #define __arch__x86__

   #include <exos/common/arch/generic_x86/x86_utils.h>
   #include <exos/common/arch/generic_x86/cpu_features.h>
   #include <exos/kernel/arch/i386/asm_defs.h>
   #include <exos/kernel/arch/i386/arch_utils.h>
   #include <exos/kernel/arch/generic_x86/fpu_memcpy.h>

#elif defined(__x86_64__)

   #define __arch__x86__

   #include <exos/common/arch/generic_x86/x86_utils.h>
   #include <exos/common/arch/generic_x86/cpu_features.h>
   #include <exos/kernel/arch/x86_64/arch_utils.h>
   #include <exos/kernel/arch/generic_x86/fpu_memcpy.h>

#else

   #error Unsupported architecture.

#endif


typedef void (*interrupt_handler)(regs *);
typedef int (*irq_interrupt_handler)(regs *);

void reboot();
void setup_segmentation(void);
void setup_soft_interrupt_handling(void);
void setup_syscall_interfaces(void);
void set_kernel_stack(uptr stack);
void enable_cpu_features(void);
void fpu_context_begin(void);
void fpu_context_end(void);
void save_current_fpu_regs(bool in_kernel);
void restore_current_fpu_regs(bool in_kernel);

