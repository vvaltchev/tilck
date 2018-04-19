
#pragma once
#include <common/basic_defs.h>
#include <exos/hal.h>
#include <exos/interrupts.h>

extern volatile u32 __fault_resume_mask;
regs *saved_fault_resumable_regs;

static inline bool in_fault_resumable_code(void)
{
   return __fault_resume_mask != 0;
}

static inline bool is_fault_resumable(u32 fault_num)
{
   ASSERT(fault_num <= 31);
   return (1 << fault_num) & __fault_resume_mask;
}

void handle_resumable_fault(regs *r);
int fault_resumable(u32 faults_mask, void *func, u32 nargs, ...);
