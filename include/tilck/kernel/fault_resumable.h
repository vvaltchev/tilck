/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/sched.h>

static inline bool in_fault_resumable_code(void)
{
   return get_curr_task()->faults_resume_mask != 0;
}

static inline bool is_fault_resumable(int fault_num)
{
   ASSERT(fault_num <= 31);
   return (1u << fault_num) & get_curr_task()->faults_resume_mask;
}

static inline int get_fault_num(u32 r)
{
   for (int i = 0; i < 32; i++)
      if (r & (1 << i))
         return i;

   return -1;
}

void handle_resumable_fault(regs_t *r);
u32 fault_resumable_call(u32 faults_mask, void *func, u32 nargs, ...);

/*
 * Fast-path fault hook for the user-access copy primitives. If the current
 * task is inside copy_{to,from}_user() (its user_access_fixup is armed),
 * redirect the faulting context to the primitive's fixup landing pad and
 * return true. This resumes in place -- no stack unwinding -- so it is far
 * cheaper than the generic fault_resumable_call() machinery. 'out_of_mem'
 * selects the errno reported to the caller (-ENOMEM vs -EFAULT).
 */
bool user_access_resume_on_fault(regs_t *r, bool out_of_mem);

/*
 * Policy for a CoW page fault that ran out of memory (COW_NO_MEM): recover via
 * the fault-resumable machinery when the kernel was accessing user memory
 * (copy_to_user() & friends), kill the process on a user-mode fault, or panic
 * for a genuinely-unguarded in-kernel access.
 */
void handle_cow_out_of_mem(regs_t *r);

