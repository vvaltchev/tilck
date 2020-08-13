/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck_gen_headers/mod_acpi.h>
#include <tilck/common/basic_defs.h>

void early_init_acpi_module(void);
void acpi_set_root_pointer(ulong);
