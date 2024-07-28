/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/process_int.h>

static inline void
switch_to_task_pop_nested_interrupts(void)
{
   if (KRN_TRACK_NESTED_INTERR) {

      ASSERT(get_curr_task() != NULL);

      if (get_curr_task()->running_in_kernel)
         if (!is_kernel_thread(get_curr_task()))
            nested_interrupts_drop_top_syscall();
   }
}


static inline void
adjust_nested_interrupts_for_task_in_kernel(struct task *ti)
{
   /*
    * The new task was running in kernel when it was preempted.
    *
    * In theory, there's nothing we have to do here, and that's exactly
    * what happens when KRN_TRACK_NESTED_INTERR is 0. But, our nice
    * debug feature for nested interrupts tracking requires a little work:
    * because of its assumptions (hard-coded in ASSERTS) are that when the
    * kernel is running, it's always inside some kind of interrupt handler
    * (fault, int 0x80 [syscall], IRQ) before resuming the next task, we have
    * to resume the state of the nested_interrupts in one case: the one when
    * we're resuming a USER task that was running in KERNEL MODE (the kernel
    * was running on behalf of the task). In that case, when for the first
    * time the user task got to the kernel, we had a nice 0x80 added in our
    * nested_interrupts array [even in the case of sysenter] by the function
    * syscall_entry(). The kernel started to work on behalf of the
    * user process but, for some reason (typically kernel preemption or
    * wait on condition) the task was scheduled out. When that happened,
    * because of the function switch_to_task_pop_nested_interrupts() called
    * above, the 0x80 value was dropped from `nested_interrupts`. Now that
    * we have to resume the execution of the user task (but in kernel mode),
    * we MUST push back that 0x80 in order to compensate the pop that will
    * occur in kernel's syscall_entry() just before returning back
    * to the user. That's because the nested_interrupts array is global and
    * not specific to any given task. Like the registers, it has to be saved
    * and restored in a consistent way.
    */

   if (!is_kernel_thread(ti)) {
      push_nested_interrupt(SYSCALL_SOFT_INTERRUPT);
   }
}
