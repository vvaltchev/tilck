/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>

#include "acpi_int.h"

static struct list batteries_list = STATIC_LIST_INIT(batteries_list);

struct battery {

   struct list_node node;

   ACPI_HANDLE *handle;
   struct basic_battery_info bi;
};

bool
acpi_is_battery(ACPI_HANDLE obj)
{
   /*
    * Evalutating the _BIX method requires the installation of an additional
    * address space handler (see AcpiInstallAddressSpaceHandler() in ACPICA and
    * OperationRegion in the ACPI spec) for the embedded controller on some
    * machines. Tilck has no support for that, yet.
    *
    * For the reasons above, force-setting `has_BIX` to false.
    */
   bool has_BIX = false;
   bool has_BIF = acpi_has_method(obj, "_BIF");
   bool has_BST = acpi_has_method(obj, "_BST");
   bool has_STA = acpi_has_method(obj, "_STA");

   return (has_BIF || has_BIX) && has_BST && has_STA;
}

ACPI_STATUS
acpi_battery_get_basic_info(ACPI_HANDLE obj, struct basic_battery_info *bi)
{
   ACPI_STATUS rc;
   ACPI_BUFFER res;
   ACPI_OBJECT *bif;
   ACPI_OBJECT *elems;
   int pu_idx = 0;
   int dcap_idx = 1;
   bool has_BIX = false;

   res.Length = ACPI_ALLOCATE_BUFFER;
   res.Pointer = NULL;

   bi->bif_method = "_BIF";
   bi->lfc_idx = 2;
   bi->design_cap = BATT_UNKNOWN_CHARGE;

   // See the comments in acpi_is_battery().
   // has_BIX = acpi_has_method(obj, "_BIX");

   if (has_BIX) {

      bi->bif_method = "_BIX";
      bi->lfc_idx++;
      bi->has_BIX = true;
      pu_idx++;
      dcap_idx++;

   } else {

      bi->has_BIX = false;
   }

   rc = AcpiEvaluateObject(obj, (char *)bi->bif_method, NULL, &res);

   if (ACPI_FAILURE(rc))
      return rc;

   bif = res.Pointer;

   if (bif->Package.Count >= 3) {

      elems = bif->Package.Elements;

      if (elems[pu_idx].Integer.Value == 0)
         bi->power_unit = "mWh";
      else if (elems[pu_idx].Integer.Value == 1)
         bi->power_unit = "mAh";
      else
         bi->power_unit = NULL; /* unknown */

      bi->design_cap = (ulong) elems[dcap_idx].Integer.Value;
   }

   ACPI_FREE(res.Pointer);
   return AE_OK;
}

static ACPI_STATUS
batter_get_last_charge_cap(ACPI_HANDLE *obj,
                           struct basic_battery_info *bi,
                           ulong *value)
{
   ACPI_STATUS rc;
   ACPI_BUFFER res;
   ACPI_OBJECT *bif;

   res.Length = ACPI_ALLOCATE_BUFFER;
   res.Pointer = NULL;

   rc = AcpiEvaluateObject(obj, (char *)bi->bif_method, NULL, &res);

   if (ACPI_FAILURE(rc))
      return rc;

   bif = res.Pointer;

   if (bif->Type != ACPI_TYPE_PACKAGE)
      goto type_error;

   if (bi->lfc_idx >= bif->Package.Count)
      goto type_error;

   if (bif->Package.Elements[bi->lfc_idx].Type != ACPI_TYPE_INTEGER)
      goto type_error;

   /* Common case */
   *value = (ulong) bif->Package.Elements[bi->lfc_idx].Integer.Value;

out:
   ACPI_FREE(res.Pointer);
   return rc;

type_error:
   rc = AE_TYPE;
   goto out;
}

static ACPI_STATUS
acpi_battery_get_charge_per_mille(struct battery *bat, ulong *ret)
{
   ACPI_STATUS rc;
   ACPI_BUFFER res;
   ACPI_OBJECT *bst, *obj_val;
   ulong cap = BATT_UNKNOWN_CHARGE;
   ulong rem = 0;

   res.Length = ACPI_ALLOCATE_BUFFER;
   res.Pointer = NULL;

   if (!bat->bi.design_cap)
      return AE_NOT_EXIST;    /* Empty batter slot */

   rc = batter_get_last_charge_cap(bat->handle, &bat->bi, &cap);

   if (ACPI_FAILURE(rc))
      return rc;

   if (cap == BATT_UNKNOWN_CHARGE)
      goto unknown_charge_err;

   rc = AcpiEvaluateObject(bat->handle, "_BST", NULL, &res);

   if (ACPI_FAILURE(rc))
      return rc;

   bst = res.Pointer;

   if (bst->Type != ACPI_TYPE_PACKAGE)
      goto type_error;

   if (bst->Package.Count < 4)
      goto type_error;

   /*
    * Point to the "Battery Remaining Capacity" field
    * ACPI 6.3 spec, table 10-333: BST Return Package Values.
    */
   obj_val = &bst->Package.Elements[2];

   if (obj_val->Type != ACPI_TYPE_INTEGER)
      goto type_error;

   /* De-reference the value */
   rem = (ulong) obj_val->Integer.Value;

   if (rem == BATT_UNKNOWN_CHARGE)
      goto unknown_charge_err;

   if (rem > cap) {

      /*
       * While charging, on some machines, the remaining capacity will be set
       * to the design capacity. Without the current check, that leads to a
       * charge% greater than 100%.
       */

      rem = cap;
   }

   if (cap > 0)
      *ret = rem * 1000 / cap; /* common case */
   else if (!rem)
      *ret = 0;                /* weird case: cap == rem == 0 */
   else
      rc = AE_ERROR;           /* non-sense: cap == 0, but rem > 0 */

out:
   ACPI_FREE(res.Pointer);
   return rc;

type_error:
   rc = AE_TYPE;
   goto out;

unknown_charge_err:
   rc = AE_NOT_EXIST;
   goto out;
}

int
acpi_get_all_batteries_charge_per_mille(ulong *ret)
{
   ulong charge_per_mille_sum = 0;
   ulong batt_cnt = 0;
   struct battery *bat;
   ACPI_STATUS rc;

   list_for_each_ro(bat, &batteries_list, node) {

      ulong tmp;

      if (!bat->bi.design_cap)
         continue; /* empty battery slot */

      rc = acpi_battery_get_charge_per_mille(bat, &tmp);

      if (ACPI_FAILURE(rc)) {

         switch (rc) {

            case AE_NO_MEMORY:
               return -ENOMEM;

            case AE_NOT_EXIST:
               return -ENOENT;

            case AE_TYPE:
               return -EPROTOTYPE; /* Note: abusing UNIX errors :-) */

            default:
               return -EIO;
         }
      }

      charge_per_mille_sum += tmp;
      batt_cnt++;
   }

   if (!batt_cnt)
      return -ENOENT;

   *ret = charge_per_mille_sum / batt_cnt;
   return 0;
}

static ACPI_STATUS
on_battery_cb(void *obj_handle,
              void *device_info,
              void *ctx)
{
   struct battery *b;
   ACPI_STATUS rc;

   if (!(b = kzalloc_obj(struct battery)))
      return AE_NO_MEMORY;

   list_node_init(&b->node);
   b->handle = obj_handle;
   rc = acpi_battery_get_basic_info(obj_handle, &b->bi);

   if (ACPI_FAILURE(rc)) {
      kfree_obj(b, struct battery);
      return rc;
   }

   list_add_tail(&batteries_list, &b->node);
   return AE_OK;
}

__attribute__((constructor))
static void __reg_batteries_cbs(void)
{
   static struct acpi_reg_per_object_cb_node batteries = {
      .cb = &on_battery_cb,
      .filter = &acpi_is_battery
   };

   list_node_init(&batteries.node);
   acpi_reg_per_object_cb(&batteries);
}
