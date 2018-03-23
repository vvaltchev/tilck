#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <random>
#include <memory>

#include <gtest/gtest.h>
#include "mocks.h"
#include "kernel_init_funcs.h"

extern "C" {

   #include <common/utils.h>

   #include <exos/kmalloc.h>
   #include <exos/paging.h>
   #include <exos/self_tests/self_tests.h>

   extern bool mock_kmalloc;
   extern bool suppress_printk;
   extern kmalloc_heap heaps[KMALLOC_HEAPS_COUNT];
   void selftest_kmalloc_perf_per_size(int size);
   void kmalloc_dump_heap_stats(void);
   void *node_to_ptr(kmalloc_heap *h, int node, size_t size);
}

using namespace std;
using namespace testing;


#define HALF(x) ((x) >> 1)
#define TWICE(x) ((x) << 1)

#define NODE_LEFT(n) (TWICE(n) + 1)
#define NODE_RIGHT(n) (TWICE(n) + 2)
#define NODE_PARENT(n) (HALF(n-1))
#define NODE_IS_LEFT(n) (((n) & 1) != 0)


u32 calculate_node_size(kmalloc_heap *h, int node)
{
   int i;
   int curr = node;

   for (i = 0; ; i++) {

      if (curr == 0)
         break;

      curr = NODE_PARENT(curr);
   }

   return h->size >> i;
}

void save_heaps_metadata(unique_ptr<u8[]> *meta_before)
{
   for (int h = 0; h < KMALLOC_HEAPS_COUNT; h++) {
      memmove(meta_before[h].get(),
              heaps[h].metadata_nodes,
              heaps[h].metadata_size);
   }
}

void print_node_info(int h, int node)
{
   const u32 node_size = calculate_node_size(&heaps[h], node);
   u8 *after = (u8*)heaps[h].metadata_nodes;

   printf("[HEAP %i] Node #%i\n", h, node);
   printf("Node size: %u\n", node_size);
   printf("Node ptr:  %p\n", node_to_ptr(&heaps[h], node, node_size));
   printf("Value:     %u\n", after[node]);
}

void check_heaps_metadata(unique_ptr<u8[]> *meta_before)
{
   for (int h = 0; h < KMALLOC_HEAPS_COUNT; h++) {

      u8 *meta_ptr = meta_before[h].get();
      kmalloc_heap *heap = &heaps[h];

      for (int i = 0; i < heap->metadata_size; i++) {

         if (meta_ptr[i] == ((u8*)heap->metadata_nodes)[i])
            continue;

         print_node_info(h, i);
         printf("Exp value: %i\n", meta_ptr[i]);
         FAIL();
      }
   }
}

void kmalloc_chaos_test_sub(default_random_engine &e,
                            lognormal_distribution<> &dist)
{
   vector<pair<void *, size_t>> allocations;

   for (int i = 0; i < 1000; i++) {

      size_t s = round(dist(e));

      if (s == 0)
         continue;

      void *r = kmalloc(s);

      if (r != NULL) {
         allocations.push_back(make_pair(r, s));
      }
   }

   for (const auto& e : allocations) {
      kfree2(e.first, e.second);
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
   selftest_kmalloc_perf();
}

TEST_F(kmalloc_test, glibc_malloc_comparative_perf_test)
{
   mock_kmalloc = true;
   selftest_kmalloc_perf();
   mock_kmalloc = false;
}

TEST_F(kmalloc_test, chaos_test)
{
   random_device rdev;
   const auto seed = rdev();
   default_random_engine e(seed);
   cout << "[ INFO     ] random seed: " << seed << endl;

   lognormal_distribution<> dist(5.0, 3);

   unique_ptr<u8[]> meta_before[KMALLOC_HEAPS_COUNT];

   for (int h = 0; h < KMALLOC_HEAPS_COUNT; h++) {

      if (!heaps[h].size)
         continue;

      meta_before[h].reset(new u8[heaps[h].metadata_size]);
   }

   for (int i = 0; i < 150; i++) {

      save_heaps_metadata(meta_before);

      ASSERT_NO_FATAL_FAILURE({
         kmalloc_chaos_test_sub(e, dist);
      });

      ASSERT_NO_FATAL_FAILURE({
         check_heaps_metadata(meta_before);
      });
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
