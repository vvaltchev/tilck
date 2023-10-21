/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/modules.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/uefi.h>

#include <tilck/mods/pci.h>
#include <tilck/mods/acpi.h>

#include "osl.h"
#include "acpi_int.h"
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

static struct list on_full_init_cb_list
   = STATIC_LIST_INIT(on_full_init_cb_list);

static struct list per_acpi_object_cb_list
   = STATIC_LIST_INIT(per_acpi_object_cb_list);

void acpi_reg_on_subsys_enabled_cb(struct acpi_reg_callback_node *cbnode)
{
   list_add_tail(&on_subsystem_enabled_cb_list, &cbnode->node);
}

void acpi_reg_on_full_init_cb(struct acpi_reg_callback_node *cbnode)
{
   list_add_tail(&on_full_init_cb_list, &cbnode->node);
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

static void
call_on_full_init_cbs(void)
{
   struct acpi_reg_callback_node *pos;

   list_for_each_ro(pos, &on_full_init_cb_list, node) {
      pos->cb(pos->ctx);
   }
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

   AcpiDbgLevel = (ACPI_NORMAL_DEFAULT | ACPI_LV_EVENTS) & ~ACPI_LV_REPAIR;
   //AcpiGbl_TraceDbgLevel = ACPI_TRACE_LEVEL_ALL;
   //AcpiGbl_TraceDbgLayer = ACPI_TRACE_LAYER_ALL;

   if (!is_uefi_boot()) {
      /*
       * Unfortunately, when we're not booting using UEFI, at least in some
       * cases (e.g. QEMU), memory regions that ACPICA would like to write
       * to perform automatic repairs are not marked as ACPI_RECLAIMABLE (3)
       * but as RESERVED (2) instead. Even if this might be a defect in QEMU's
       * legacy BIOS firmware, we should handle that somehow. On a first glance
       * it might look like that we could just temporarily map as R/W all the
       * RESERVED regions during the ACPI initialization. Unfortunately, it
       * looks like that functions like AcpiNsRepair_HID() could be called
       * inconditionally every time when an ACPI object is evaluated and there
       * is some metadata with expectations about that object. It looks like
       * that ACPI doesn't check if writing is really necessary before doing so.
       * For example, in AcpiNsRepair_HID(), the TOUPPER operation is performed
       * even when the string is already completely in upper case. Fixing ACPICA
       * to not do any repairs when it's not strictly necessary might be an
       * alternative (because we evaluate all the objects during the init), but
       * then it would be cumbersome to upgrade ACPICA because of those custom
       * changes. Therefore, the simplest thing to do now is just to disable the
       * auto repair when we're not booting with UEFI. In addition to that, we
       * need to map the regions of type 3 (ACPI_RECLAIMABLE) as R/W.
       */
      AcpiGbl_DisableAutoRepair = TRUE;
   }

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
call_per_matching_device_cbs(ACPI_HANDLE obj, ACPI_DEVICE_INFO *Info)
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

      if (pos->filter && !pos->filter(obj))
         continue; // The filter discarded the object

      rc = pos->cb(obj, Info, pos->ctx);

      if (ACPI_FAILURE(rc))
         return rc;
   }

   return AE_OK;
}

static ACPI_STATUS
acpi_walk_single_obj_with_info(ACPI_HANDLE parent,
                               ACPI_HANDLE obj,
                               ACPI_DEVICE_INFO *Info)
{
   ACPI_STATUS rc;

   if (Info->Type == ACPI_TYPE_DEVICE) {

      rc = call_per_matching_device_cbs(obj, Info);

      if (rc == AE_NO_MEMORY)
         return rc; /* Only the OOM condition requires the walk to stop */
   }

   rc = register_acpi_obj_in_sysfs(parent, obj, Info);

   if (rc == AE_NO_MEMORY)
      return rc;

   return AE_OK;
}

static ACPI_STATUS
acpi_walk_single_obj(ACPI_HANDLE parent, ACPI_HANDLE obj)
{
   ACPI_DEVICE_INFO *Info;
   ACPI_STATUS rc;

   /* Get object's info */
   rc = AcpiGetObjectInfo(obj, &Info);

   if (ACPI_FAILURE(rc)) {
      print_acpi_failure("AcpiGetObjectInfo", NULL, rc);
      return rc; /* Fatal error */
   }

   /* Call the per-obj function */
   rc = acpi_walk_single_obj_with_info(parent, obj, Info);

   ACPI_FREE(Info);
   return rc;
}

static ACPI_STATUS
acpi_walk_ns(void)
{
   ACPI_HANDLE parent, child;
   ACPI_STATUS rc;

   printk("ACPI: walk through all objects in the namespace\n");

   parent = NULL; /* means root */
   child = NULL;  /* means first child */

   rc = register_acpi_obj_in_sysfs(parent, child, NULL);

   if (rc == AE_NO_MEMORY)
      return rc;

   while (true) {

      rc = AcpiGetNextObject(ACPI_TYPE_ANY, parent, child, &child);

      if (ACPI_FAILURE(rc)) {

         /* No more children */

         if (!parent)
            break; /* This was the root: stop */

         /* Go back upwards */
         child = parent;
         AcpiGetParent(parent, &parent);
         continue;
      }

      /* Call the per-obj function */
      rc = acpi_walk_single_obj(parent, child);

      if (ACPI_FAILURE(rc))
         return rc;  /* Likely, out-of-memory (OOM) condition. */

      /* Check if `child` has any children */
      rc = AcpiGetNextObject(ACPI_TYPE_ANY, child, NULL, NULL);

      if (ACPI_SUCCESS(rc)) {

         /* Yes, it does. Do DFS. */
         parent = child;
         child = NULL;     /* first child */
      }
   }

   return AE_OK;
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

   rc = acpi_walk_ns();

   if (ACPI_FAILURE(rc)) {
      print_acpi_failure("acpi_walk_ns", NULL, rc);
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

   printk("ACPI: Call on-full-init callbacks\n");
   call_on_full_init_cbs();
}

bool
acpi_has_method(ACPI_HANDLE obj, const char *name)
{
   ACPI_HANDLE ret;
   return ACPI_SUCCESS(AcpiGetHandle(obj, (ACPI_STRING)name, &ret));
}

static void
acpi_module_init(void)
{
   if (kopt_noacpi) {
      printk("ACPI: don't load tables and switch to ACPI mode (-noacpi)\n");
      return;
   }

   acpi_mod_load_tables();
   acpi_mod_enable_subsystem();
}

static struct module acpi_module = {

   .name = "acpi",
   .priority = MOD_acpi_prio,
   .init = &acpi_module_init,
};

REGISTER_MODULE(&acpi_module);
