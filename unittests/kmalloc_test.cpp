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
   void kernel_kmalloc_perf_test_per_size(int size);
}

using namespace std;
using namespace testing;

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
      ASSERT_EQ(e.first, ptr);
   }

   for (const auto& e : allocations) {
      kfree(e.first, e.second);
      mem_allocated -= e.second;
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

TEST_F(kmalloc_test, perf32B_test)
{
   for (int i = 0; i < 10; i++)
      kernel_kmalloc_perf_test_per_size(32);
}

TEST_F(kmalloc_test, perf256K_test)
{
   for (int i = 0; i < 10; i++)
      kernel_kmalloc_perf_test_per_size(256 * KB);
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
   default_random_engine e(rdev());

   lognormal_distribution<> dist(5.0, 3);

   for (int i = 0; i < 100; i++) {
      kmalloc_chaos_test_sub(e, dist);
   }
}

extern "C" {
bool kbasic_virtual_alloc(uptr vaddr, int page_count);
bool kbasic_virtual_free(uptr vaddr, int page_count);
}

TEST_F(kmalloc_test, kbasic_virtual_alloc)
{
   bool success;

   uptr vaddr = KERNEL_BASE_VA + 5 * MB;

   success = kbasic_virtual_alloc(vaddr, 1);
   ASSERT_TRUE(success);

   ASSERT_TRUE(is_mapped(get_kernel_page_dir(), (void *)vaddr));

   success = kbasic_virtual_free(vaddr, 1);
   ASSERT_TRUE(success);
}
