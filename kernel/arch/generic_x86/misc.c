/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_kb8042.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/mods/acpi.h>

NORETURN void poweroff(void)
{
   printk("Halting the system...\n");

   if (MOD_acpi) {
      if (get_acpi_init_status() >= ais_subsystem_enabled) {
         acpi_poweroff();
      }
   }

   disable_interrupts_forced();

   /*
    * We get here if one of the following statements is true:
    *
    *    - No acpi module
    *    - The acpi initialization failed
    *    - The acpi poweroff procedure failed
    *
    * Therefore, try the qemu debug power off method, if we're running on a
    * hypervisor.
    */

   if (in_hypervisor())
      debug_qemu_turn_off_machine();

   /*
    * Eveything failed. Just print a message confirming that's safe to turn off
    * the machine.
    */

   printk("System halted.\n");

   while (true) {
      halt();
   }
}

/* Reboot the machine, using the best method available */
NORETURN void reboot(void)
{
   printk("Rebooting the machine...\n");

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
    *
    * Therefore, try the legacy i8042 reset.
    */

   if (MOD_kb8042)
      i8042_reboot();

   panic("Unable to reboot the machine");
}
