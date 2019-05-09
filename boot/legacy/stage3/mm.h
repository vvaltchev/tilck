/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#define MEM_USABLE             1
#define MEM_RESERVED           2
#define MEM_ACPI_RECLAIMABLE   3
#define MEM_ACPI_NVS_MEMORY    4
#define MEM_BAD                5

typedef struct {

   u64 base;
   u64 len;
   u32 type;
   u32 acpi;

} mem_area_t;

typedef struct {

   mem_area_t *mem_areas;
   u32 count;

} mem_info;

/*
 * Reads from BIOS system's memory map and store it in the ma_buffer.
 * At the end, it sets the fields of the mem_info structure `mi`.
 */
void read_memory_map(void *buffer, size_t buf_size, mem_info *mi);
void poison_usable_memory(mem_info *mi);

/*
 * Get the first usable memory area of size `size` with address >= `min_paddr`.
 * Returns `0` in case of failure.
 */
uptr get_usable_mem(mem_info *mi, uptr min_paddr, uptr size);

/* Wrapper of get_usable_mem() which triggers PANIC instead of returning 0 */
uptr get_usable_mem_or_panic(mem_info *mi, uptr min_paddr, uptr size);

static inline u32 bios_to_multiboot_mem_region(u32 bios_mem_type)
{
   STATIC_ASSERT(MEM_USABLE == MULTIBOOT_MEMORY_AVAILABLE);
   STATIC_ASSERT(MEM_RESERVED == MULTIBOOT_MEMORY_RESERVED);
   STATIC_ASSERT(MEM_ACPI_RECLAIMABLE == MULTIBOOT_MEMORY_ACPI_RECLAIMABLE);
   STATIC_ASSERT(MEM_ACPI_NVS_MEMORY == MULTIBOOT_MEMORY_NVS);
   STATIC_ASSERT(MEM_BAD == MULTIBOOT_MEMORY_BADRAM);

   return bios_mem_type;
}
