/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/sched.h>

#include "osl.h"
#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/acexcep.h>

static ACPI_TABLE_DESC acpi_tables[16];

void
early_init_acpi_module(void)
{
   ACPI_STATUS rc;

   rc = AcpiInitializeSubsystem();

   if (rc != AE_OK)
      panic("AcpiInitializeSubsystem() failed with: %d", rc);

   rc = AcpiInitializeTables(acpi_tables, 16, false);

   if (rc != AE_OK)
      panic("AcpiInitializeTables() failed with: %d", rc);
}
