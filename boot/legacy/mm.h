/* SPDX-License-Identifier: BSD-2-Clause */

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

u32 read_memory_map(mem_area_t *mem_areas);
void poison_usable_memory(mem_area_t *mem_areas, u32 mem_areas_count);

static inline u32 bios_to_multiboot_mem_region(u32 bios_mem_type)
{
   STATIC_ASSERT(MEM_USABLE == MULTIBOOT_MEMORY_AVAILABLE);
   STATIC_ASSERT(MEM_RESERVED == MULTIBOOT_MEMORY_RESERVED);
   STATIC_ASSERT(MEM_ACPI_RECLAIMABLE == MULTIBOOT_MEMORY_ACPI_RECLAIMABLE);
   STATIC_ASSERT(MEM_ACPI_NVS_MEMORY == MULTIBOOT_MEMORY_NVS);
   STATIC_ASSERT(MEM_BAD == MULTIBOOT_MEMORY_BADRAM);

   return bios_mem_type;
}
