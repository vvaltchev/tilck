
#pragma once

#include <common_defs.h>
#include <process.h>
#include <arch/generic_x86/utils.h>

#define X86_PC_TIMER_IRQ       0
#define X86_PC_KEYBOARD_IRQ    1
#define X86_PC_RTC_IRQ         8
#define X86_PC_ACPI_IRQ        9
#define X86_PC_PS2_MOUSE_IRQ  12

// Forward-declaring regs
typedef struct regs regs;


/* This defines what the stack looks like after an ISR ran */
struct regs {
   u32 gs, fs, es, ds;      /* pushed the segs last */
   u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by 'pusha' */
   u32 int_num, err_code;    /* our 'push byte #' and error codes do this */
   u32 eip, cs, eflags, useresp, ss;   /* pushed by the CPU automatically */
};

static ALWAYS_INLINE void set_return_register(regs *r, u32 value)
{
   r->eax = value;
}


NORETURN void asm_context_switch_x86(u32 d, ...);

NORETURN static ALWAYS_INLINE void context_switch(regs *r)
{
   asm_context_switch_x86(
                          // Segment registers
                          r->gs,
                          r->fs,
                          r->es,
                          r->ds,

                          // General purpose registers
                          r->edi,
                          r->esi,
                          r->ebp,
                          /* skipping ESP */
                          r->ebx,
                          r->edx,
                          r->ecx,
                          r->eax,

                          // Registers pop-ed by iret
                          r->eip,
                          r->cs,
                          r->eflags,
                          r->useresp,
                          r->ss);
}
