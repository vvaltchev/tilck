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
#include <utils.h>
}

using namespace std;



void init_test_kmalloc()
{
   uptr align_size = 16 * PAGE_SIZE;

   uptr addr = (uptr)malloc(HEAP_DATA_SIZE + align_size);
   addr += align_size;
   addr &= ~(align_size - 1);

   kernel_heap_base = (void *)addr;
}


void kmalloc_chaos_test_sub(default_random_engine &e,
                            lognormal_distribution<> &dist)
{
   size_t mem_allocated = 0;
   vector<pair<void *, size_t>> allocations;

   for (int i = 0; i < 1000; i++) {

      size_t orig_s = round(dist(e));
      size_t s = roundup_next_power_of_2(MAX(orig_s, MIN_BLOCK_SIZE));

      void *r = kmalloc(s);

      if (!r) {
         continue;
      }

      mem_allocated += s;
      allocations.push_back(make_pair(r, s));
   }

   for (const auto& e : allocations) {
      kfree(e.first, e.second);
      mem_allocated -= e.second;
   }

   // Now, re-allocate all the chunks and expect to get the same results.

   for (const auto& e : allocations) {

      void *ptr = kmalloc(e.second);

      if (ptr != e.first) {
         printf("TEST FAILED\n");
         printf("ptr expected: %p\n", e.first);
         printf("ptr got:      %p\n", ptr);
         break;
      }
   }

   for (const auto& e : allocations) {
      kfree(e.first, e.second);
      mem_allocated -= e.second;
   }
}

void kmalloc_chaos_test()
{
   random_device rdev;
   default_random_engine e(rdev());

   lognormal_distribution<> dist(5.0, 3);

   for (int i = 0; i < 100; i++) {
      kmalloc_chaos_test_sub(e, dist);
   }
}
