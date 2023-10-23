/* SPDX-License-Identifier: BSD-2-Clause */

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <random>

using namespace std;

extern "C" {

#include <tilck/common/utils.h>

#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/kmalloc.h>
#include <kernel/kmalloc/kmalloc_heap_struct.h> // kmalloc private header
#include <kernel/kmalloc/kmalloc_block_node.h>  // kmalloc private header
#include <tilck/kernel/test/mem_regions.h>
#include <tilck/kernel/test/kmalloc.h>

extern bool suppress_printk;

void *base_va = nullptr;
static unordered_map<ulong, ulong> mappings;

void initialize_test_kernel_heap()
{
   const ulong test_mem_size = 256 * MB;

   if (base_va != nullptr) {
      bzero(base_va, test_mem_size);
      mappings.clear();
      return;
   }

   base_va = aligned_alloc(MB, test_mem_size);
   bzero(base_va, test_mem_size);

   mem_regions_count = 1;
   mem_regions[0] = (struct mem_region) {
      .addr = 0,
      .len = test_mem_size,
      .type = MULTIBOOT_MEMORY_AVAILABLE,
      .extra = 0,
   };
}

void init_kmalloc_for_tests()
{
   bzero(&kmalloc_initialized, sizeof(kmalloc_initialized));
   bzero((void *)&first_heap_struct, sizeof(first_heap_struct));
   bzero(&heaps, sizeof(heaps));
   bzero(&used_heaps, sizeof(used_heaps));
   bzero(&max_tot_heap_mem_free, sizeof(max_tot_heap_mem_free));

   initialize_test_kernel_heap();
   suppress_printk = true;
   early_init_kmalloc();
   init_kmalloc();
   suppress_printk = false;
}

int map_page(pdir_t *, void *vaddr, ulong paddr, u32 pg_flags)
{
   ASSERT(!((ulong)vaddr & OFFSET_IN_PAGE_MASK)); // check page-aligned
   ASSERT(!(paddr & OFFSET_IN_PAGE_MASK)); // check page-aligned

   mappings[(ulong)vaddr] = paddr;
   return 0;
}

size_t
map_pages(pdir_t *pdir,
          void *vaddr,
          ulong paddr,
          size_t page_count,
          u32 pg_flags)
{
   for (size_t i = 0; i < page_count; i++) {
      int rc = map_page(pdir,
                        (char *)vaddr + (i << PAGE_SHIFT),
                        paddr + (i << PAGE_SHIFT),
                        0);
      VERIFY(rc == 0);
   }

   return page_count;
}

void unmap_page(pdir_t *, void *vaddrp, bool free_pageframe)
{
   mappings[(ulong)vaddrp] = INVALID_PADDR;
}

int unmap_page_permissive(pdir_t *, void *vaddrp, bool free_pageframe)
{
   unmap_page(nullptr, vaddrp, free_pageframe);
   return 0;
}

void
unmap_pages(pdir_t *pdir,
            void *vaddr,
            size_t count,
            bool do_free)
{
   for (size_t i = 0; i < count; i++) {
      unmap_page(pdir, (char *)vaddr + (i << PAGE_SHIFT), do_free);
   }
}

size_t unmap_pages_permissive(pdir_t *pd, void *va, size_t count, bool do_free)
{
   for (size_t i = 0; i < count; i++) {
      unmap_page_permissive(pd, (char *)va + (i << PAGE_SHIFT), do_free);
   }

   return count;
}

bool is_mapped(pdir_t *, void *vaddrp)
{
   ulong vaddr = (ulong)vaddrp & PAGE_MASK;

   if (vaddr + PAGE_SIZE < LINEAR_MAPPING_END)
      return true;

   return mappings.find(vaddr) != mappings.end();
}

ulong get_mapping(pdir_t *, void *vaddrp)
{
   return mappings[(ulong)vaddrp];
}

int virtual_read(pdir_t *pdir, void *extern_va, void *dest, size_t len)
{
   memcpy(dest, extern_va, len);
   return 0;
}

int virtual_write(pdir_t *pdir, void *extern_va, void *src, size_t len)
{
   memcpy(extern_va, src, len);
   return 0;
}

} // extern "C"
