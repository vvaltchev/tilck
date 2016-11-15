#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <random>

extern "C" {
#include <kmalloc.h>
#include <paging.h>
void kmalloc_trivial_perf_test();
}

using namespace std;

extern "C" {


void *kernel_heap_base = nullptr;

void *__wrap_get_kernel_page_dir()
{
   return nullptr;
}

unordered_map<uintptr_t, bool> mappings;

bool __wrap_is_mapped(void *pdir, uintptr_t vaddr)
{
   return mappings[vaddr & ~(PAGE_SIZE - 1)];
}

bool __wrap_kbasic_virtual_alloc(uintptr_t vaddr, int pageCount)
{
   assert((vaddr & (PAGE_SIZE - 1)) == 0);

   for (int i = 0; i < pageCount; i++) {
      mappings[vaddr + i * PAGE_SIZE] = true;
   }

   return true;
}

bool __wrap_kbasic_virtual_free(uintptr_t vaddr, int pageCount)
{
   assert((vaddr & (PAGE_SIZE - 1)) == 0);

   for (int i = 0; i < pageCount; i++) {
      mappings[vaddr + i * PAGE_SIZE] = false;
   }

   return true;
}

}

void init_test_kmalloc()
{
   uintptr_t addr = (uintptr_t)malloc(HEAP_DATA_SIZE + PAGE_SIZE);
   addr += PAGE_SIZE;
   addr &= ~(PAGE_SIZE - 1);

   kernel_heap_base = (void *)addr;
}

void kmalloc_chaos_test()
{
   random_device rdev;
   default_random_engine e(rdev());

   lognormal_distribution<> dist(5.0, 3);

   size_t mem_allocated = 0;
   vector<pair<void *, size_t>> allocations;

   for (int i = 0; i < 100; i++) {

      //cout << round(dist(e)) << endl;

      size_t s = roundup_next_power_of_2(round(dist(e)));

      printf("Allocating %u bytes..\n", s);

      void *r = kmalloc(s);

      if (!r) {

         printf("Unable to allocate %u bytes (allocated by now: %u)\n", s, mem_allocated);
         continue;
      }

      allocations.push_back(make_pair(r, s));
   }
}


int main(int argc, char **argv) {

   init_test_kmalloc();
   initialize_kmalloc();

   printf("kernel heap base: %p\n", kernel_heap_base);
   //kmalloc_trivial_perf_test();

   kmalloc_chaos_test();

   return 0;
}
