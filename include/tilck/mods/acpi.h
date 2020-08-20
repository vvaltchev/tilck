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

#if MOD_acpi

static inline enum acpi_init_status
get_acpi_init_status(void)
{
   extern enum acpi_init_status acpi_init_status;
   return acpi_init_status;
}

void acpi_mod_init_tables(void);
void acpi_set_root_pointer(ulong);

#else

static inline enum acpi_init_status
get_acpi_init_status(void) { return ais_not_started; }

static inline void acpi_mod_init_tables(void) { }
static inline void acpi_set_root_pointer(ulong ptr) { }

#endif
