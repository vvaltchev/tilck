
#pragma once

#include <commonDefs.h>
#include <process.h>

// Forward-declaring regs
typedef struct regs regs;


/* This defines what the stack looks like after an ISR ran */
struct regs {
   u32 gs, fs, es, ds;      /* pushed the segs last */
   u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by 'pusha' */
   u32 int_no, err_code;    /* our 'push byte #' and error codes do this */
   u32 eip, cs, eflags, useresp, ss;   /* pushed by the processor automatically */
};