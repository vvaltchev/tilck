
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

#include <exos/common/utils.h>

#include <exos/kernel/system_mmap.h>
#include <exos/kernel/kmalloc.h>
#include <exos/kernel/paging.h>

extern bool kmalloc_initialized;
extern bool suppress_printk;

void *kernel_va = nullptr;
bool mock_kmalloc = false;

static unordered_map<uptr, uptr> mappings;

void initialize_test_kernel_heap()
{
   uptr test_mem_size = 256 * MB;

   if (kernel_va != nullptr) {
      bzero(kernel_va, test_mem_size);
      mappings.clear();
      return;
   }

   kernel_va = aligned_alloc(MB, test_mem_size);

   mem_regions_count = 1;
   mem_regions[0] = (memory_region_t) {
      .addr = 0,
      .len = test_mem_size,
      .type = MULTIBOOT_MEMORY_AVAILABLE,
      .extra = 0
   };
}

void init_kmalloc_for_tests()
{
   kmalloc_initialized = false;
   initialize_test_kernel_heap();
   suppress_printk = true;
   init_kmalloc();
   suppress_printk = false;
}

void map_page(page_directory_t *, void *vaddr, uptr paddr, bool us, bool rw)
{
   ASSERT(!((uptr)vaddr & OFFSET_IN_PAGE_MASK)); // check page-aligned
   ASSERT(!(paddr & OFFSET_IN_PAGE_MASK)); // check page-aligned

   mappings[(uptr)vaddr] = paddr;
}

void unmap_page(page_directory_t *, void *vaddrp)
{
   mappings[(uptr)vaddrp] = INVALID_PADDR;
}

bool is_mapped(page_directory_t *, void *vaddrp)
{
   uptr vaddr = (uptr)vaddrp & PAGE_MASK;

   if (vaddr + PAGE_SIZE < LINEAR_MAPPING_OVER_END)
      return true;

   return mappings.find(vaddr) != mappings.end();
}

uptr get_mapping(page_directory_t *, void *vaddrp)
{
   return mappings[(uptr)vaddrp];
}

void *__real_kmalloc(size_t size);
void __real_kfree2(void *ptr, size_t size);

void *__wrap_kmalloc(size_t size)
{
   if (mock_kmalloc)
      return malloc(size);

   return __real_kmalloc(size);
}

void __wrap_kfree2(void *ptr, size_t size)
{
   if (mock_kmalloc)
      return free(ptr);

   __real_kfree2(ptr, size);
}

}
