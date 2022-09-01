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

bool
arch_specific_new_task_setup(struct task *ti, struct task *parent)
{
   NOT_IMPLEMENTED();
}

void
arch_specific_free_task(struct task *ti)
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

NODISCARD int
kthread_create2(kthread_func_ptr func, const char *name, int fl, void *arg)
{
   NOT_IMPLEMENTED();
}

NORETURN void
switch_to_task(struct task *ti)
{
   NOT_IMPLEMENTED();
}

void
finalize_usermode_task_setup(struct task *ti, regs_t *user_regs)
{
   NOT_IMPLEMENTED();
}

int setup_process(struct elf_program_info *pinfo,
                  struct task *ti,
                  const char *const *argv,
                  const char *const *env,
                  struct task **ti_ref,
                  regs_t *r)
{
   NOT_IMPLEMENTED();
}

void setup_pause_trampoline(regs_t *r)
{
   NOT_IMPLEMENTED();
}

int setup_sig_handler(struct task *ti,
                      enum sig_state sig_state,
                      regs_t *r,
                      ulong user_func,
                      int signum)
{
   NOT_IMPLEMENTED();
}

void task_info_reset_kernel_stack(struct task *ti)
{
   NOT_IMPLEMENTED();
}

void save_current_task_state(regs_t *r)
{
   NOT_IMPLEMENTED();
}
