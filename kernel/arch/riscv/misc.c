/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/debug_utils.h>

void init_textmode_console(void)
{
   panic("ERROR: riscv don't support textmode console\n");
}

NORETURN void poweroff(void)
{
   NOT_IMPLEMENTED();
}

/* Reboot the machine, using the best method available */
NORETURN void reboot(void)
{
   NOT_IMPLEMENTED();
}
