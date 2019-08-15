/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>

/* Reboot the machine, using the best method available */
void reboot(void)
{
   /* At the moment, only the reboot through the PS/2 controller is supported */
   x86_pc_8042_reboot();
}
