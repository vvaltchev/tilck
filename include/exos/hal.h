
#pragma once
#define __EXOS_HAL__

#include <common/basic_defs.h>

#if defined(__i386__)

   #define __arch__x86__
   #include <common/arch/generic_x86/x86_utils.h>
   #include <exos/arch/i386/arch_utils.h>

#elif defined(__x86_64__)

   #define __arch__x86__
   #include <common/arch/generic_x86/x86_utils.h>
   #include <exos/arch/x86_64/arch_utils.h>

#else

   #error Unsupported architecture.

#endif



#ifdef __arch__x86__

   void gdt_install(void);
   void idt_install(void);

   #define setup_segmentation() gdt_install()
   #define setup_interrupt_handling() idt_install(); irq_install()

#endif

typedef void (*interrupt_handler)(regs *);
void setup_sysenter_interface();
void set_kernel_stack(uptr stack);
uptr get_kernel_stack();
