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

#include <kernel/kmalloc/kmalloc_heap_struct.h> // kmalloc private header
#include <kernel/kmalloc/kmalloc_block_node.h>  // kmalloc private header

extern bool suppress_printk;

extern bool kmalloc_initialized;
extern struct kmalloc_heap first_heap_struct;
extern struct kmalloc_heap *heaps[KMALLOC_HEAPS_COUNT];
extern u32 used_heaps;
extern size_t max_tot_heap_mem_free;
extern struct mem_region mem_regions[MAX_MEM_REGIONS];
extern int mem_regions_count;

void *kernel_va = nullptr;
bool mock_kmalloc = false;

static unordered_map<ulong, ulong> mappings;

void initialize_test_kernel_heap()
{
   const ulong test_mem_size = 256 * MB;

   if (kernel_va != nullptr) {
      bzero(kernel_va, test_mem_size);
      mappings.clear();
      return;
   }

   kernel_va = aligned_alloc(MB, test_mem_size);
   bzero(kernel_va, test_mem_size);

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
   bzero(&first_heap_struct, sizeof(first_heap_struct));
   bzero(&heaps, sizeof(heaps));
   bzero(&used_heaps, sizeof(used_heaps));
   bzero(&max_tot_heap_mem_free, sizeof(max_tot_heap_mem_free));

   initialize_test_kernel_heap();
   suppress_printk = true;
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

void *kmalloc(size_t size)
{
   if (mock_kmalloc)
      return malloc(size);

   return general_kmalloc(&size, 0);
}

void kfree2(void *ptr, size_t size)
{
   if (mock_kmalloc)
      return free(ptr);

   return general_kfree(ptr, &size, 0);
}

void *kmalloc_get_first_heap(size_t *size)
{
   static void *buf;

   if (!buf) {
      buf = aligned_alloc(KMALLOC_MAX_ALIGN, KMALLOC_FIRST_HEAP_SIZE);
      VERIFY(buf);
      VERIFY( ((ulong)buf & (KMALLOC_MAX_ALIGN - 1)) == 0 );
   }

   if (size)
      *size = KMALLOC_FIRST_HEAP_SIZE;

   return buf;
}

} // extern "C"
