/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_tracing.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/signal.h>
#include <tilck/mods/tracing.h>

void *get_syscall_func_ptr(u32 n)
{
   NOT_IMPLEMENTED();
}

int get_syscall_num(void *func)
{
   NOT_IMPLEMENTED();
}

void handle_syscall(regs_t *r)
{
   NOT_IMPLEMENTED();
}

void init_syscall_interfaces(void)
{
   /* do nothing */
}
