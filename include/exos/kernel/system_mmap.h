
#pragma once
#include <exos/common/basic_defs.h>
#include <system_headers/multiboot.h>

#define MAX_MEM_REGIONS 512

#define MEM_REG_EXTRA_RAMDISK  1
#define MEM_REG_EXTRA_KERNEL   2
#define MEM_REG_EXTRA_LOWMEM   4

typedef struct {

   u64 addr;
   u64 len;
   u32 type;  /* multiboot_memory_map_t's type */
   u32 extra; /* bit mask */

} memory_region_t;

extern memory_region_t mem_regions[MAX_MEM_REGIONS];
extern int mem_regions_count;
