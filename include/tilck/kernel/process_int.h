
#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>
#include <tilck/kernel/sched.h>

/* Internal stuff (used by process.c, process32.c, misc.c, sched.c) */
extern char *kernel_initial_stack[KERNEL_INITIAL_STACK_SIZE];
void switch_to_initial_kernel_stack(void);
static ALWAYS_INLINE void set_current_task(task_info *ti)
{
   atomic_store_explicit(&__current, ti, mo_relaxed);
}
