/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/errno.h>

#include <elf.h>

void dump_stacktrace(void *ebp, pdir_t *pdir)
{
   NOT_IMPLEMENTED();
}

void dump_sstatus(ulong sstatus)
{
   NOT_IMPLEMENTED();
}

void dump_regs(regs_t *r)
{
   NOT_IMPLEMENTED();
}

int debug_qemu_turn_off_machine(void)
{
   NOT_IMPLEMENTED();
}

void dump_raw_stack(ulong addr)
{
   NOT_IMPLEMENTED();
}
