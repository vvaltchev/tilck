/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_kb8042.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>

/* Reboot the machine, using the best method available */
void reboot(void)
{
   if (MOD_kb8042) {
      i8042_reboot();
   } else {
      printk("WARNING: unable to reboot: the mod kb8042 is not built-in\n");
   }
}
