
#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/sched.h>

/* Internal stuff (used by process.c, process32.c, misc.c, sched.c) */
extern char *kernel_initial_stack[KERNEL_STACK_SIZE];
void switch_to_initial_kernel_stack(void);

static ALWAYS_INLINE void set_curr_task(task_info *ti)
{
   DEBUG_ONLY(check_not_in_irq_handler());
   ASSERT(!are_interrupts_enabled());
   __current = ti;
}
