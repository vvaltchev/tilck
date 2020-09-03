/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include "acpi_int.h"

static ACPI_STATUS
acpi_fix_power_button_handler(void *ctx)
{
   printk("ACPI: power button fixed event\n");
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

static void
powerbtn_notify_handler(ACPI_HANDLE Device,
                        UINT32 Value,
                        void *Context)
{
   printk("ACPI: power button notify event, device: %p, val: %#x\n",
          Device, Value);
}

static ACPI_STATUS
powerbtn_reg_notify_cb(void *obj_handle,
                       void *device_info,
                       void *ctx)
{
   ACPI_STATUS rc;
   printk("ACPI: register notify handler for power button\n");

   rc = AcpiInstallNotifyHandler(obj_handle,
                                 ACPI_ALL_NOTIFY,
                                 &powerbtn_notify_handler,
                                 NULL);

   if (ACPI_FAILURE(rc)) {
      print_acpi_failure("AcpiInstallNotifyHandler", NULL, rc);
      return rc;
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

   static struct acpi_reg_per_object_cb_node notf = {
      .cb = &powerbtn_reg_notify_cb,
      .ctx = NULL,
      .hid = "PNP0C0C",
      .uid = NULL,
      .cls = NULL,
   };

   list_node_init(&fixh.node);
   acpi_reg_on_subsys_enabled_cb(&fixh);

   list_node_init(&notf.node);
   acpi_reg_per_object_cb(&notf);
}
