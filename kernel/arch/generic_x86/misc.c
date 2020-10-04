/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_kb8042.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>

#include <tilck/kernel/sched.h>
#include <tilck/mods/acpi.h>

/* Reboot the machine, using the best method available */
void reboot(void)
{
   disable_preemption();
   disable_interrupts_forced();

   if (MOD_acpi) {
      if (get_acpi_init_status() >= ais_subsystem_enabled) {
         acpi_reboot();
      }
   }

   /*
    * We get here if one of the following statements is true:
    *
    *    - No acpi module
    *    - The acpi initialization failed
    *    - The acpi reboot procedure failed
    */

   if (MOD_kb8042)
      i8042_reboot();

   panic("Unable to reboot the machine");
}
