/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/sched.h>
#include <tilck/mods/acpi.h>

#include "osl.h"
#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/accommon.h>

enum acpi_init_status acpi_init_status;

void
print_acpi_failure(const char *func, ACPI_STATUS rc)
{
   const ACPI_EXCEPTION_INFO *ex = AcpiUtValidateException(rc);

   if (ex) {
      printk("ERROR: %s() failed with: %s\n", func, ex->Name);
   } else {
      printk("ERROR: %s() failed with: %d\n", func, rc);
   }
}

void
acpi_mod_init_tables(void)
{
   ACPI_STATUS rc;

   ASSERT(acpi_init_status == ais_not_started);
   AcpiDbgLevel = ACPI_DEBUG_DEFAULT;
   AcpiGbl_TraceDbgLevel = ACPI_TRACE_LEVEL_ALL;
   AcpiGbl_TraceDbgLayer = ACPI_TRACE_LAYER_ALL;

   rc = AcpiInitializeSubsystem();

   if (rc != AE_OK) {
      print_acpi_failure("AcpiInitializeSubsystem", rc);
      acpi_init_status = ais_failed;
      return;
   }

   rc = AcpiInitializeTables(NULL, 0, true);

   if (rc != AE_OK) {
      print_acpi_failure("AcpiInitializeTables", rc);
      acpi_init_status = ais_failed;
      return;
   }

   acpi_init_status = ais_tables_initialized;
}

void
acpi_mod_load_tables(void)
{
   ACPI_STATUS rc;

   if (acpi_init_status == ais_failed)
      return;

   ASSERT(acpi_init_status == ais_tables_initialized);
   rc = AcpiLoadTables();

   if (rc != AE_OK) {
      print_acpi_failure("AcpiLoadTables", rc);
      acpi_init_status = ais_failed;
      return;
   }

   acpi_init_status = ais_tables_loaded;
}
