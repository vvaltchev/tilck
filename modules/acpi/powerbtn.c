/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include "acpi_int.h"

static ACPI_STATUS
acpi_fix_power_button_handler(void *ctx)
{
   printk("ACPI: power button fix event\n");
   return 0; /* MUST return 0 in ANY case. Other values are reserved */
}

static ACPI_STATUS
powerbtn_reg_fix_handlers(void *__ctx)
{
   ACPI_STATUS rc;

   rc = AcpiInstallFixedEventHandler(ACPI_EVENT_POWER_BUTTON,
                                     &acpi_fix_power_button_handler,
                                     NULL);

   if (ACPI_FAILURE(rc)) {
      print_acpi_failure("AcpiInstallFixedEventHandler", NULL, rc);
      /* NOTE: Don't consider it as a fatal failure */
   }

   return AE_OK;
}

__attribute__((constructor))
static void __reg_callbacks(void)
{
   static struct acpi_reg_callback_node fixh = {
      .cb = &powerbtn_reg_fix_handlers,
      .ctx = NULL
   };

   list_node_init(&fixh.node);
   acpi_reg_on_subsys_enabled_cb(&fixh);
}
