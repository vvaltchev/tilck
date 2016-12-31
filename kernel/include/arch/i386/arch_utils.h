
#pragma once

#include <common_defs.h>
#include <process.h>
#include <arch/generic_x86/utils.h>

// Forward-declaring regs
typedef struct regs regs;


/* This defines what the stack looks like after an ISR ran */
struct regs {
   u32 gs, fs, es, ds;      /* pushed the segs last */
   u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by 'pusha' */
   u32 int_no, err_code;    /* our 'push byte #' and error codes do this */
   u32 eip, cs, eflags, useresp, ss;   /* pushed by the CPU automatically */
};

static inline void set_return_register(regs *r, u32 value)
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
