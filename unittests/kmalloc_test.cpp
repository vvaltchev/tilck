#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <random>

#include <gtest/gtest.h>
#include "mocks.h"
#include "kernel_init_funcs.h"

extern "C" {
   #include <kmalloc.h>
   #include <paging.h>
   #include <utils.h>
   #include <self_tests/self_tests.h>
   extern bool mock_kmalloc;
   extern bool suppress_printk;
   void kernel_kmalloc_perf_test_per_size(int size);
   void kmalloc_dump_heap_stats(void);
   extern kmalloc_heap heaps[KMALLOC_HEAPS_COUNT];
}

using namespace std;
using namespace testing;

void kmalloc_chaos_test_sub(default_random_engine &e,
                            lognormal_distribution<> &dist,
                            unique_ptr<u8> *meta_before)
{
   vector<pair<void *, size_t>> allocations;

   for (int h = 0; h < KMALLOC_HEAPS_COUNT; h++) {
      memmove(meta_before[h].get(),
              heaps[h].metadata_nodes,
              heaps[h].metadata_size);
   }

   for (int i = 0; i < 1000; i++) {

      size_t s = round(dist(e));

      if (s == 0)
         continue;

      void *r = kmalloc(s);

      if (!r) {
         continue;
      }

      allocations.push_back(make_pair(r, s));
   }

   for (const auto& e : allocations) {
      kfree(e.first, e.second);
   }

   for (int h = 0; h < KMALLOC_HEAPS_COUNT; h++) {
      for (int i = 0; i < heaps[h].metadata_size; i++) {
         u8 *after = (u8*)heaps[h].metadata_nodes;
         if (meta_before[h].get()[i] != after[i]) {
            printf("[HEAP %i] Meta before and after differ at byte %i!\n", h,i);
            printf("Before: %u\n", meta_before[h].get()[i]);
            printf("After:  %u\n", after[i]);
            FAIL();
         }
      }
   }
}

class kmalloc_test : public Test {
public:

   void SetUp() override {
      initialize_kmalloc_for_tests();
   }

   void TearDown() override {
      /* do nothing, for the moment */
   }
};

TEST_F(kmalloc_test, perf_test)
{
   kernel_kmalloc_perf_test();
}

TEST_F(kmalloc_test, glibc_malloc_comparative_perf_test)
{
   mock_kmalloc = true;
   kernel_kmalloc_perf_test();
   mock_kmalloc = false;
}

TEST_F(kmalloc_test, DISABLED_chaos_test)
{
   random_device rdev;
   const auto seed = 1394506485;
   //const auto seed = rdev();
   default_random_engine e(seed);
   cout << "[ INFO     ] random seed: " << seed << endl;

   lognormal_distribution<> dist(5.0, 3);

   unique_ptr<u8> meta_before[KMALLOC_HEAPS_COUNT];

   for (int h = 0; h < KMALLOC_HEAPS_COUNT; h++) {
      meta_before[h].reset((u8*)malloc(heaps[h].metadata_size));
   }

   for (int i = 0; i < 10; i++) {

      printk("*** ITER %i ***\n", i);

      if (i <= 5)
         suppress_printk = true;

      ASSERT_NO_FATAL_FAILURE({
         kmalloc_chaos_test_sub(e, dist, meta_before);
      });

      suppress_printk = false;
   }
}

extern "C" {
bool kbasic_virtual_alloc(uptr vaddr, int page_count);
void kbasic_virtual_free(uptr vaddr, int page_count);
}

TEST_F(kmalloc_test, kbasic_virtual_alloc)
{
   bool success;

   uptr vaddr = KERNEL_BASE_VA + 5 * MB;

   success = kbasic_virtual_alloc(vaddr, 1);
   ASSERT_TRUE(success);

   ASSERT_TRUE(is_mapped(get_kernel_page_dir(), (void *)vaddr));

   kbasic_virtual_free(vaddr, 1);
}
