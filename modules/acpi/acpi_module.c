/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/sched.h>

#include "osl.h"
#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/acexcep.h>

void
early_init_acpi_module(void)
{
   ACPI_STATUS rc;

   //AcpiDbgLevel = 0xffffffff; /* Max debug level */
   AcpiDbgLevel = ACPI_DEBUG_DEFAULT;
   AcpiGbl_TraceDbgLevel = ACPI_TRACE_LEVEL_ALL;
   AcpiGbl_TraceDbgLayer = ACPI_TRACE_LAYER_ALL;

   rc = AcpiInitializeSubsystem();

   if (rc != AE_OK) {
      printk("ERROR: AcpiInitializeSubsystem() failed with: %d", rc);
      return;
   }

   rc = AcpiInitializeTables(NULL, 0, true);

   if (rc != AE_OK) {
      printk("ERROR: AcpiInitializeTables() failed with: %d", rc);
      return;
   }
}
