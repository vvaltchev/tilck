
#pragma once


#if defined(__i386__)
#define __arch__x86__

#include <arch/generic_x86/x86_utils.h>
#include <arch/i386/arch_utils.h>


static ALWAYS_INLINE int regs_intnum(regs *r)
{
   return r->int_num;
}

#elif defined(__x86_64__)
#define __arch__x86__

#include <arch/generic_x86/x86_utils.h>

struct regs {
   /* STUB struct */
};

typedef struct regs regs;

static ALWAYS_INLINE int regs_intnum(regs *r)
{
   /* STUB implementation */
   return 0;
}

static ALWAYS_INLINE void set_return_register(regs *r, u32 value)
{
   /* STUB implementation */
}

#else

#error Unsupported architecture.

#endif

#ifdef __arch__x86__

void gdt_install();
void idt_install();

#define setup_segmentation() gdt_install()
#define setup_interrupt_handling() idt_install(); irq_install()
#endif

typedef void (*interrupt_handler)(regs *);

void setup_sysenter_interface();

void set_kernel_stack(uptr stack);
uptr get_kernel_stack();

void disable_preemption();
void enable_preemption();
bool is_preemption_enabled();


