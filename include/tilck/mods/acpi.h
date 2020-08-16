/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck_gen_headers/mod_acpi.h>
#include <tilck/common/basic_defs.h>

enum acpi_init_status {

   ais_not_started = 0,
   ais_tables_initialized,
   ais_tables_loaded,
   ais_failed,
};

static inline enum acpi_init_status
get_acpi_init_status(void)
{
   extern enum acpi_init_status acpi_init_status;
   return acpi_init_status;
}

void early_init_acpi_module(void);
void acpi_set_root_pointer(ulong);
