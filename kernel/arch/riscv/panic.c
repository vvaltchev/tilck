/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/system_mmap.h>

volatile bool __in_panic;
volatile bool __in_double_fault;
volatile bool __in_kernel_shutdown;
volatile bool __in_panic_debugger;

void init_console(void);         /* defined in main.c */
void panic_save_current_state(); /* defined in kernel_yield.S */
regs_t panic_state_regs;

/* Called by the assembly function panic_save_current_state() */
void panic_save_current_task_state(regs_t *r)
{
   NOT_IMPLEMENTED();
}

NORETURN void panic(const char *fmt, ...)
{
   NOT_IMPLEMENTED();
}
