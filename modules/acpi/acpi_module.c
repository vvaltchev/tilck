/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/modules.h>
#include <tilck/mods/acpi.h>

#include "osl.h"
#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/accommon.h>

/* Global APIC initialization status */
enum acpi_init_status acpi_init_status;

/* Revision number of the FADT table */
static u8 acpi_fadt_revision;

/* HW flags read from FADT */
static u16 acpi_iapc_boot_arch;
static u32 acpi_fadt_flags;

void
print_acpi_failure(const char *func, const char *farg, ACPI_STATUS rc)
{
   const ACPI_EXCEPTION_INFO *ex = AcpiUtValidateException(rc);

   if (ex) {
      printk("ERROR: %s(%s) failed with: %s\n", func, farg ? farg:"", ex->Name);
   } else {
      printk("ERROR: %s(%s) failed with: %d\n", func, farg ? farg : "", rc);
   }
}

enum tristate
acpi_is_8042_present(void)
{
   ASSERT(acpi_init_status >= ais_tables_initialized);

   if (acpi_fadt_revision >= 3) {

      if (acpi_iapc_boot_arch & ACPI_FADT_8042)
         return tri_yes;

      return tri_no;
   }

   return tri_unknown;
}

enum tristate
acpi_is_vga_text_mode_avail(void)
{
   ASSERT(acpi_init_status >= ais_tables_initialized);

   if (acpi_fadt_revision >= 4) {

      if (acpi_iapc_boot_arch & ACPI_FADT_NO_VGA)
         return tri_no;

      return tri_yes;
   }

   return tri_unknown;
}

static void
acpi_read_acpi_hw_flags(void)
{
   ACPI_STATUS rc;
   struct acpi_table_fadt *fadt;

   rc = AcpiGetTable(ACPI_SIG_FADT, 1, (struct acpi_table_header **)&fadt);

   if (rc == AE_NOT_FOUND)
      return;

   if (ACPI_FAILURE(rc)) {
      print_acpi_failure("AcpiGetTable", "FADT", rc);
      return;
   }

   acpi_fadt_revision = fadt->Header.Revision;
   acpi_iapc_boot_arch = fadt->BootFlags;
   acpi_fadt_flags = fadt->Flags;

   AcpiPutTable((struct acpi_table_header *)fadt);
}

void
acpi_mod_init_tables(void)
{
   ACPI_STATUS rc;

   ASSERT(acpi_init_status == ais_not_started);
   AcpiDbgLevel = ACPI_DEBUG_DEFAULT;
   AcpiGbl_TraceDbgLevel = ACPI_TRACE_LEVEL_ALL;
   AcpiGbl_TraceDbgLayer = ACPI_TRACE_LAYER_ALL;

   printk("ACPI: AcpiInitializeSubsystem\n");
   rc = AcpiInitializeSubsystem();

   if (ACPI_FAILURE(rc)) {
      print_acpi_failure("AcpiInitializeSubsystem", NULL, rc);
      acpi_init_status = ais_failed;
      return;
   }

   printk("ACPI: AcpiInitializeTables\n");
   rc = AcpiInitializeTables(NULL, 0, true);

   if (ACPI_FAILURE(rc)) {
      print_acpi_failure("AcpiInitializeTables", NULL, rc);
      acpi_init_status = ais_failed;
      return;
   }

   acpi_init_status = ais_tables_initialized;
   acpi_read_acpi_hw_flags();
}

void
acpi_mod_load_tables(void)
{
   ACPI_STATUS rc;

   if (acpi_init_status == ais_failed)
      return;

   ASSERT(acpi_init_status == ais_tables_initialized);

   printk("ACPI: AcpiLoadTables\n");
   rc = AcpiLoadTables();

   if (ACPI_FAILURE(rc)) {
      print_acpi_failure("AcpiLoadTables", NULL, rc);
      acpi_init_status = ais_failed;
      return;
   }

   acpi_init_status = ais_tables_loaded;
}

void
acpi_mod_enable_subsystem(void)
{
   ACPI_STATUS rc;

   if (acpi_init_status == ais_failed)
      return;

   ASSERT(acpi_init_status == ais_tables_loaded);
   ASSERT(is_preemption_enabled());

   AcpiUpdateInterfaces(ACPI_DISABLE_ALL_STRINGS);
   AcpiInstallInterface("Windows 2000");

   printk("ACPI: AcpiEnableSubsystem\n");
   rc = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);

   if (ACPI_FAILURE(rc)) {
      print_acpi_failure("AcpiEnableSubsystem", NULL, rc);
      acpi_init_status = ais_failed;
      return;
   }

   acpi_init_status = ais_subsystem_enabled;

   printk("ACPI: AcpiInitializeObjects\n");
   rc = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);

   if (ACPI_FAILURE(rc)) {

      print_acpi_failure("AcpiInitializeObjects", NULL, rc);
      acpi_init_status = ais_failed;

      printk("ACPI: AcpiTerminate\n");
      rc = AcpiTerminate();

      if (ACPI_FAILURE(rc))
         print_acpi_failure("AcpiTerminate", NULL, rc);

      return;
   }

   acpi_init_status = ais_fully_initialized;
}

static void
acpi_module_init(void)
{
   acpi_mod_load_tables();
   acpi_mod_enable_subsystem();
}

static struct module acpi_module = {

   .name = "acpi",
   .priority = MOD_acpi_prio,
   .init = &acpi_module_init,
};

REGISTER_MODULE(&acpi_module);
