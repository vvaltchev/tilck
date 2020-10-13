/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/mods/acpi.h>

#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/accommon.h>

#define BATT_UNKNOWN_CHARGE            0xffffffff

struct basic_battery_info {

   ulong design_cap;
   const char *power_unit;
   const char *bif_method;    /* _BIF or _BIX */
   u32 lfc_idx;               /* Last Full Charge Capacity Property Index */
   bool has_BIX;
};

void
print_acpi_failure(const char *func, const char *farg, ACPI_STATUS rc);

ACPI_STATUS
register_acpi_obj_in_sysfs(ACPI_HANDLE parent_obj,
                           ACPI_HANDLE obj,
                           ACPI_DEVICE_INFO *obj_info);

bool
acpi_has_method(ACPI_HANDLE obj, const char *name);

bool
acpi_is_battery(ACPI_HANDLE obj);

ACPI_STATUS
acpi_battery_get_basic_info(ACPI_HANDLE obj, struct basic_battery_info *bi);
