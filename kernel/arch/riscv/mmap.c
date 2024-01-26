/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/system_mmap_int.h>

#include "paging_int.h"

void arch_add_initial_mem_regions()
{
   /*
    * Reserve 1MB below kernel's head, where
    * the flattened device tree is located.
    */
   append_mem_region((struct mem_region) {
      .addr = KERNEL_VA_TO_PA(KERNEL_VADDR) - (1 * MB),
      .len = 1 * MB,
      .type = MULTIBOOT_MEMORY_RESERVED,
      .extra = MEM_REG_EXTRA_LOWMEM,
   });
}

bool arch_add_final_mem_regions()
{
   /* do nothing */
   return true;
}

