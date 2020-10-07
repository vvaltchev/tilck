/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/modules.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/pci.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/debug_utils.h>
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

/* Callback lists */
static struct list on_subsystem_enabled_cb_list
   = STATIC_LIST_INIT(on_subsystem_enabled_cb_list);

static struct list per_acpi_object_cb_list
   = STATIC_LIST_INIT(per_acpi_object_cb_list);

void acpi_reg_on_subsys_enabled_cb(struct acpi_reg_callback_node *cbnode)
{
   list_add_tail(&on_subsystem_enabled_cb_list, &cbnode->node);
}

void acpi_reg_per_object_cb(struct acpi_reg_per_object_cb_node *cbnode)
{
   list_add_tail(&per_acpi_object_cb_list, &cbnode->node);
}

static ACPI_STATUS
call_on_subsys_enabled_cbs(void)
{
   STATIC_ASSERT(sizeof(u32) == sizeof(ACPI_STATUS));

   struct acpi_reg_callback_node *pos;
   ACPI_STATUS rc;

   list_for_each_ro(pos, &on_subsystem_enabled_cb_list, node) {

      rc = (ACPI_STATUS)pos->cb(pos->ctx);

      if (ACPI_FAILURE(rc))
         return rc;
   }

   return AE_OK;
}

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
acpi_reboot(void)
{
   struct acpi_table_fadt *fadt = &AcpiGbl_FADT;

   printk("Performing ACPI reset...\n");

   if (acpi_fadt_revision < 2) {
      printk("ACPI reset failed: not supported (FADT too old)\n");
      return;
   }

   if (~acpi_fadt_flags & ACPI_FADT_RESET_REGISTER) {
      printk("ACPI reset failed: not supported\n");
      return;
   }

   if (fadt->ResetRegister.SpaceId == ACPI_ADR_SPACE_PCI_CONFIG) {

      pci_config_write(

         pci_make_loc(
            0,                                            /* segment */
            0,                                            /* bus */
            (fadt->ResetRegister.Address >> 32) & 0xffff, /* device */
            (fadt->ResetRegister.Address >> 16) & 0xffff  /* function */
         ),

         fadt->ResetRegister.Address & 0xffff,            /* offset */
         8,                                               /* width */
         fadt->ResetValue
      );

   } else {

      /* Supports both the memory and I/O port spaces */
      AcpiReset();
   }

   /* Ok, now just loop tight for a bit, while the machine resets */
   for (int i = 0; i < 100; i++)
      delay_us(10 * 1000);

   /* If we got here, something really weird happened */
   printk("ACPI reset failed for an unknown reason\n");
}

void
acpi_poweroff(void)
{
   ACPI_STATUS rc;
   ASSERT(are_interrupts_enabled());

   rc = AcpiEnterSleepStatePrep(ACPI_STATE_S5);

   if (ACPI_FAILURE(rc)) {
      print_acpi_failure("AcpiEnterSleepStatePrep", NULL, rc);
      return;
   }

   /* AcpiEnterSleepState() requires to be called with interrupts disabled */
   disable_interrupts_forced();

   rc = AcpiEnterSleepState(ACPI_STATE_S5);

   /*
    * In theory, we should never get here but, in practice, everything could
    * happen.
    */

   print_acpi_failure("AcpiEnterSleepState", NULL, rc);
}

void
acpi_mod_init_tables(void)
{
   ACPI_STATUS rc;

   ASSERT(acpi_init_status == ais_not_started);

   AcpiDbgLevel = ACPI_NORMAL_DEFAULT | ACPI_LV_EVENTS;
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

static ACPI_STATUS
call_per_matching_device_cbs(ACPI_HANDLE ObjHandle, ACPI_DEVICE_INFO *Info)
{
   struct acpi_reg_per_object_cb_node *pos;
   const char *hid, *uid, *cls;
   u32 hid_l, uid_l, cls_l;
   ACPI_STATUS rc;

   hid = (Info->Valid & ACPI_VALID_HID) ? Info->HardwareId.String : NULL;
   hid_l = Info->HardwareId.Length;

   uid = (Info->Valid & ACPI_VALID_UID) ? Info->UniqueId.String : NULL;
   uid_l = Info->UniqueId.Length;

   cls = (Info->Valid & ACPI_VALID_CLS) ? Info->ClassCode.String : NULL;
   cls_l = Info->ClassCode.Length;

   list_for_each_ro(pos, &per_acpi_object_cb_list, node) {

      if (pos->hid && (!hid || strncmp(hid, pos->hid, hid_l)))
         continue; // HID doesn't match

      if (pos->uid && (!uid || strncmp(uid, pos->uid, uid_l)))
         continue; // UID doesn't match

      if (pos->cls && (!cls || strncmp(cls, pos->cls, cls_l)))
         continue; // CLS doesn't match

      rc = pos->cb(ObjHandle, Info, pos->ctx);

      if (ACPI_FAILURE(rc))
         return rc;
   }

   return AE_OK;
}

static ACPI_STATUS
walk_single_dev(ACPI_HANDLE ObjHandle,
                UINT32 Level,
                void *Context,
                void **ReturnValue)
{
   ACPI_DEVICE_INFO *Info;
   ACPI_BUFFER Path;
   ACPI_STATUS rc = AE_OK;
   char buf[256];
   const char *hid;

   Path.Length = sizeof(buf);
   Path.Pointer = buf;

   /* Get the device info for this device and print it */
   rc = AcpiGetObjectInfo(ObjHandle, &Info);

   if (ACPI_FAILURE(rc)) {
      printk("ERROR: Failed to get object info\n");
      return AE_CTRL_TERMINATE;
   }

   /* Get the full path of this device and print it */
   rc = AcpiGetName(ObjHandle, ACPI_FULL_PATHNAME, &Path);

   if (ACPI_FAILURE(rc)) {
      printk("Failed to get full object name\n");
      rc = AE_CTRL_TERMINATE;
      goto out;
   }

   hid = (Info->Valid & ACPI_VALID_HID) ? Info->HardwareId.String : "n/a";
   printk("%s (%#04x, HID: %s)\n", buf, Info->Type, hid);

   rc = call_per_matching_device_cbs(ObjHandle, Info);

   if (ACPI_FAILURE(rc)) {
      *(ACPI_STATUS *)ReturnValue = rc;
      rc = AE_CTRL_TERMINATE;
   }

out:
   AcpiOsFree(Info);
   return rc;
}

static ACPI_STATUS
acpi_walk_all_devices(void)
{
   ACPI_STATUS rc, ret = AE_OK;
   printk("ACPI: dump all devices in the namespace:\n");

   rc = AcpiWalkNamespace(ACPI_TYPE_DEVICE,   // Type
                          ACPI_ROOT_OBJECT,   // StartObject
                          INT_MAX,            // MaxDepth
                          walk_single_dev,    // DescendingCallback
                          NULL,               // AscendingCallback
                          NULL,               // Context
                          (void **)&ret);     // ReturnValue

   if (ACPI_FAILURE(rc))
      return rc;

   return ret;
}

static void
acpi_handle_fatal_failure_after_enable_subsys(void)
{
   ACPI_STATUS rc;
   ASSERT(acpi_init_status >= ais_subsystem_enabled);

   acpi_init_status = ais_failed;

   printk("ACPI: AcpiTerminate\n");
   rc = AcpiTerminate();

   if (ACPI_FAILURE(rc))
      print_acpi_failure("AcpiTerminate", NULL, rc);
}

static void
acpi_global_event_handler(UINT32 EventType,
                          ACPI_HANDLE Device,
                          UINT32 EventNumber,
                          void *Context)
{
   u32 gpe;

   if (EventType == ACPI_EVENT_TYPE_FIXED) {
      printk("ACPI: fixed event #%u\n", EventNumber);
      return;
   }

   if (EventType != ACPI_EVENT_TYPE_GPE) {
      printk("ACPI: warning: unknown event type: %u\n", EventType);
      return;
   }

   /* We received a GPE */
   gpe = EventNumber;

   printk("ACPI: got GPE #%u\n", gpe);
}

void
acpi_mod_enable_subsystem(void)
{
   ACPI_STATUS rc;

   if (acpi_init_status == ais_failed)
      return;

   ASSERT(acpi_init_status == ais_tables_loaded);
   ASSERT(is_preemption_enabled());

   // AcpiUpdateInterfaces(ACPI_DISABLE_ALL_STRINGS);
   // AcpiInstallInterface("Windows 2000");

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
      acpi_handle_fatal_failure_after_enable_subsys();
      return;
   }

   /*
    * According to acpica-reference-18.pdf, 4.4.3.1, at this point we have to
    * execute all the _PRW methods and install our GPE handlers.
    */

   rc = acpi_walk_all_devices();

   if (ACPI_FAILURE(rc)) {
      print_acpi_failure("acpi_walk_all_devices", NULL, rc);
      acpi_handle_fatal_failure_after_enable_subsys();
      return;
   }

   printk("ACPI: Call on-subsys-enabled callbacks\n");
   rc = call_on_subsys_enabled_cbs();

   if (ACPI_FAILURE(rc)) {
      print_acpi_failure("call_on_subsys_enabled_cbs", NULL, rc);
      acpi_handle_fatal_failure_after_enable_subsys();
      return;
   }

   rc = AcpiInstallGlobalEventHandler(&acpi_global_event_handler, NULL);

   if (ACPI_FAILURE(rc)) {
      print_acpi_failure("AcpiInstallGlobalEventHandler", NULL, rc);
      acpi_handle_fatal_failure_after_enable_subsys();
      return;
   }

   acpi_init_status = ais_fully_initialized;
   rc = AcpiUpdateAllGpes();

   if (ACPI_FAILURE(rc)) {
      print_acpi_failure("AcpiUpdateAllGpes", NULL, rc);
      acpi_handle_fatal_failure_after_enable_subsys();
      return;
   }
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
