/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/mods/acpi.h>

#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/accommon.h>

void
print_acpi_failure(const char *func, const char *farg, ACPI_STATUS rc);

bool
acpi_has_method(ACPI_HANDLE obj, const char *name);
