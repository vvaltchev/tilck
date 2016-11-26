
#pragma once

#include <commonDefs.h>
#include <process.h>

// Forward-declaring regs
typedef struct regs regs;


/* This defines what the stack looks like after an ISR ran */
struct regs {
   uint32_t gs, fs, es, ds;      /* pushed the segs last */
   uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by 'pusha' */
   uint32_t int_no, err_code;    /* our 'push byte #' and error codes do this */
   uint32_t eip, cs, eflags, useresp, ss;   /* pushed by the processor automatically */
};

STATIC_ASSERT(sizeof(regs) == PROCESS_REGS_DATA_SIZE);