/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

void uefi_set_rt_pointer(ulong addr);
void setup_uefi_runtime_services(void);

static ALWAYS_INLINE bool is_uefi_boot(void)
{
   extern ulong uefi_rt_addr;
   return !!uefi_rt_addr;
}
