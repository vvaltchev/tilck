/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <system_headers/multiboot.h>

#define MAX_MEM_REGIONS             512

#define MEM_REG_EXTRA_RAMDISK         1
#define MEM_REG_EXTRA_KERNEL          2
#define MEM_REG_EXTRA_LOWMEM          4
#define MEM_REG_EXTRA_FRAMEBUFFER     8

struct mem_region {

   u64 addr;
   u64 len;
   u32 type;  /* multiboot_memory_map_t's type */
   u32 extra; /* bit mask */
};

void system_mmap_add_ramdisk(ulong start_paddr, ulong end_paddr);
int system_mmap_get_ramdisk(int ramdisk_index, void **va, size_t *size);
void system_mmap_set(multiboot_info_t *mbi);
int system_mmap_get_region_of(ulong paddr);
bool linear_map_mem_region(struct mem_region *r, ulong *vbegin, ulong *vend);
bool system_mmap_merge_rd_extra_region_if_any(void *rd);

static ALWAYS_INLINE int
get_mem_regions_count(void)
{
   extern int mem_regions_count;
   return mem_regions_count;
}

static ALWAYS_INLINE void
get_mem_region(int n, struct mem_region *r)
{
   extern struct mem_region mem_regions[MAX_MEM_REGIONS];
   *r = mem_regions[n];
}

static ALWAYS_INLINE ulong
get_phys_mem_mb(void)
{
   extern u32 __mem_upper_kb;
   return __mem_upper_kb >> 10;
}

static ALWAYS_INLINE ulong
get_phys_mem_size(void)
{
   extern u32 __mem_upper_kb;
   return __mem_upper_kb << 10;
}
