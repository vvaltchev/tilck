
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

void system_mmap_add_ramdisk(uptr start_paddr, uptr end_paddr);
void *system_mmap_get_ramdisk_vaddr(int ramdisk_index);
void system_mmap_set(multiboot_info_t *mbi);
int system_mmap_get_region_of(uptr paddr);
bool linear_map_mem_region(memory_region_t *r, uptr *vbegin, uptr *vend);
void dump_system_memory_map(void);

extern u32 __mem_upper_kb;

static ALWAYS_INLINE uptr get_phys_mem_mb(void)
{
   return __mem_upper_kb >> 10;
}

static ALWAYS_INLINE uptr get_phys_mem_size(void)
{
   return __mem_upper_kb << 10;
}
