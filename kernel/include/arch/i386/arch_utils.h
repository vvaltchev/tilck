
#pragma once

#include <common_defs.h>
#include <arch/generic_x86/x86_utils.h>


// Forward-declaring regs
typedef struct regs regs;


/* This defines what the stack looks like after an ISR ran */
struct regs {
   u32 gs, fs, es, ds;      /* pushed the segs last */
   u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by 'pusha' */
   u32 int_num, err_code;    /* our 'push byte #' and error codes do this */
   u32 eip, cs, eflags, useresp, ss;   /* pushed by the CPU automatically */
};

#define EFLAGS_CF    0x0001
#define EFLAGS_PF    0x0004
#define EFLAGS_AF    0x0010
#define EFLAGS_ZF    0x0040
#define EFLAGS_SF    0x0080
#define EFLAGS_TF    0x0100
#define EFLAGS_IF    0x0200
#define EFLAGS_DF    0x0400
#define EFLAGS_OF    0x0800
#define EFLAGS_NT    0x4000
#define EFLAGS_RF   0x10000
#define EFLAGS_VM   0x20000
#define EFLAGS_AC   0x40000
#define EFLAGS_VIF  0x80000
#define EFLAGS_VIP 0x100000
#define EFLAGS_ID  0x200000

#define EFLAGS_IOPL 0x3000

static ALWAYS_INLINE void set_return_register(regs *r, u32 value)
{
   r->eax = value;
}


NORETURN void asm_context_switch_x86(u32 first_reg, ...);
NORETURN void asm_kernel_context_switch_x86(u32 first_reg, ...);

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



NORETURN static ALWAYS_INLINE void kernel_context_switch(regs *r)
{
   asm_kernel_context_switch_x86(
                                 r->eip,
                                 r->useresp,

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

                                 // The eflags register
                                 r->eflags,

                                 // The useresp is repeated. See the assembly.
                                 r->useresp);
}

