
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

#include <kmalloc.h>
#include <paging.h>
#include <utils.h>

extern bool kmalloc_initialized;
void *kernel_heap_base = nullptr;
unordered_map<uptr, uptr> mappings;

void initialize_test_kernel_heap()
{
   if (kernel_heap_base != nullptr) {
      memset(kernel_heap_base, 0, HEAP_DATA_SIZE);
      mappings.clear();
      return;
   }

   kernel_heap_base = aligned_alloc(ALLOC_BLOCK_SIZE, HEAP_DATA_SIZE);
}

void initialize_kmalloc_for_tests()
{
   kmalloc_initialized = false;
   initialize_test_kernel_heap();
   init_pageframe_allocator();
   initialize_kmalloc();
}

bool __wrap_is_mapped(void *pdir, uptr vaddr)
{
   return mappings[vaddr & PAGE_MASK] != 0;
}

bool __wrap_kbasic_virtual_alloc(uptr vaddr, int page_count)
{
   assert((vaddr & (PAGE_SIZE - 1)) == 0);

   for (int i = 0; i < page_count; i++) {
      uptr p = alloc_pageframe();
      mappings[vaddr + i * PAGE_SIZE] = p;
   }

   return true;
}

bool __wrap_kbasic_virtual_free(uptr vaddr, int page_count)
{
   assert((vaddr & (PAGE_SIZE - 1)) == 0);

   for (int i = 0; i < page_count; i++) {

      uptr phys_addr = mappings[vaddr + i * PAGE_SIZE];
      free_pageframe(phys_addr);
      mappings[vaddr + i * PAGE_SIZE] = 0;
   }

   return true;
}

}
