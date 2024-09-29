
#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/hal.h>

/* Internal stuff (used by process.c, process32.c, misc.c, sched.c, fork.c) */
extern char *kernel_initial_stack[KERNEL_STACK_SIZE];

/* See the comments below in setup_sig_handler() */
#define SIG_HANDLER_ALIGN_ADJUST                        \
   (                                                    \
      (                                                 \
         + USERMODE_STACK_ALIGN                         \
         - sizeof(regs_t)               /* regs */      \
         - sizeof(ulong)                /* signum */    \
      ) % USERMODE_STACK_ALIGN                          \
   )

void switch_to_initial_kernel_stack(void);
int save_regs_on_user_stack(regs_t *r);
void restore_regs_from_user_stack(regs_t *r);
void free_common_task_allocs(struct task *ti);
void process_free_mappings_info(struct process *pi);
void task_info_reset_kernel_stack(struct task *ti);
int setup_first_process(pdir_t *pdir, struct task **ti_ref);
void finalize_usermode_task_setup(struct task *ti, regs_t *user_regs);
void setup_usermode_task_regs(regs_t *r, void *entry, void *stack_addr);
void kthread_create_init_regs_arch(regs_t *r, void *func);
void kthread_create_setup_initial_stack(struct task *ti, regs_t *r, void *arg);
void arch_usermode_task_switch(struct task *ti);
void save_curr_fpu_ctx_if_enabled(void);

static ALWAYS_INLINE void set_curr_task(struct task *ti)
{
   extern struct task *__current;

#ifndef UNIT_TEST_ENVIRONMENT
   DEBUG_ONLY(check_not_in_irq_handler());
   ASSERT(!are_interrupts_enabled());
#endif

   __current = ti;
}
