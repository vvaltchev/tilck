/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_kb8042.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>

#include <tilck/kernel/sched.h>

/* Reboot the machine, using the best method available */
void reboot(void)
{
   disable_preemption();
   disable_interrupts_forced();

   if (MOD_kb8042)
      i8042_reboot();

   panic("Unable to reboot the machine");
}
