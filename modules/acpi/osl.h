/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <3rd_party/acpi/acpi.h>

ACPI_STATUS
osl_init_malloc(void);

ACPI_STATUS
osl_init_tasks(void);

ACPI_STATUS
osl_init_irqs(void);
