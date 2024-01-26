/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/arch/riscv/sbi.h>
#include <tilck/kernel/debug_utils.h>

void init_textmode_console(void)
{
   panic("ERROR: riscv don't support textmode console\n");
}

NORETURN void poweroff(void)
{
   printk("Halting the system...\n");

   disable_interrupts_forced();

   /*
    * Just print a message confirming that's safe to turn off
    * the machine.
    */

   printk("System halted.\n");

   while (true) {
      sbi_shutdown();
   }
}

/* Reboot the machine, using the best method available */
NORETURN void reboot(void)
{
   printk("Rebooting the machine...\n");

   disable_interrupts_forced();

   sbi_system_reset(SBI_SRST_TYPE_COLD_REBOOT, SBI_SRST_REASON_NONE);

   panic("Unable to reboot the machine");
}

