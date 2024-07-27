
#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/hal.h>

/* Internal stuff (used by process.c, process32.c, misc.c, sched.c, fork.c) */
extern char *kernel_initial_stack[KERNEL_STACK_SIZE];
void switch_to_initial_kernel_stack(void);
void free_common_task_allocs(struct task *ti);
void process_free_mappings_info(struct process *pi);
void task_info_reset_kernel_stack(struct task *ti);
int setup_first_process(pdir_t *pdir, struct task **ti_ref);
void finalize_usermode_task_setup(struct task *ti, regs_t *user_regs);
void setup_usermode_task_regs(regs_t *r, void *entry, void *stack_addr);

static ALWAYS_INLINE void set_curr_task(struct task *ti)
{
   extern struct task *__current;

#ifndef UNIT_TEST_ENVIRONMENT
   DEBUG_ONLY(check_not_in_irq_handler());
   ASSERT(!are_interrupts_enabled());
#endif

   __current = ti;
}
