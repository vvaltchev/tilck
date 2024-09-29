/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_mm.h>
#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/worker_thread.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/signal.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/vdso.h>

#include <tilck/mods/tracing.h>

void
setup_usermode_task_regs(regs_t *r, void *entry, void *stack_addr)
{
   NOT_IMPLEMENTED();
}

void
arch_specific_new_proc_setup(struct process *pi, struct process *parent)
{
   NOT_IMPLEMENTED();
}

void
arch_specific_free_proc(struct process *pi)
{
   NOT_IMPLEMENTED();
}

void
kthread_create_init_regs_arch(regs_t *r, void *func)
{
   NOT_IMPLEMENTED();
}

void
kthread_create_setup_initial_stack(struct task *ti, regs_t *r, void *arg)
{
   NOT_IMPLEMENTED();
}

void
save_curr_fpu_ctx_if_enabled(void)
{
   NOT_IMPLEMENTED();
}

void
arch_usermode_task_switch(struct task *ti)
{
   NOT_IMPLEMENTED();
}

void
set_kernel_stack(ulong addr)
{
   NOT_IMPLEMENTED();
}

int
setup_sig_handler(struct task *ti,
                  enum sig_state sig_state,
                  regs_t *r,
                  ulong user_func,
                  int signum)
{
   NOT_IMPLEMENTED();
}
