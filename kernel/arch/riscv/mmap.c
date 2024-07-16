/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/system_mmap_int.h>

#include "paging_int.h"

#define MAX_DMA                                (256 * KB)

STATIC_ASSERT(MAX_DMA <= 16 * MB);
STATIC_ASSERT((MAX_DMA & (64 * KB - 1)) == 0);

void arch_add_initial_mem_regions()
{
   NOT_IMPLEMENTED();
}

bool arch_add_final_mem_regions()
{
   NOT_IMPLEMENTED();
}
