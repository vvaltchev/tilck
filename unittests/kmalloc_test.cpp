/* SPDX-License-Identifier: BSD-2-Clause */
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

   #include <tilck/common/utils.h>

   #include <tilck/kernel/kmalloc.h>
   #include <tilck/kernel/paging.h>
   #include <tilck/kernel/self_tests.h>

   #include <kernel/mm/kmalloc_heap_struct.h> // kmalloc private header
   #include <kernel/mm/kmalloc_block_node.h>  // kmalloc private header

   extern bool mock_kmalloc;
   extern bool suppress_printk;
   extern kmalloc_heap *heaps[KMALLOC_HEAPS_COUNT];
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

   for (i = 0; curr; i++) {
      curr = NODE_PARENT(curr);
   }

   return h->size >> i;
}

void save_heaps_metadata(unique_ptr<u8[]> *meta_before)
{
   for (int h = 0; h < KMALLOC_HEAPS_COUNT && heaps[h]; h++) {

      memcpy(meta_before[h].get(),
             heaps[h]->metadata_nodes,
             heaps[h]->metadata_size);
   }
}

void print_node_info(int h, int node)
{
   const u32 node_size = calculate_node_size(heaps[h], node);
   u8 *after = (u8*)heaps[h]->metadata_nodes;

   printf("[HEAP %i] Node #%i\n", h, node);
   printf("Node size: %u\n", node_size);
   printf("Node ptr:  %p\n", node_to_ptr(heaps[h], node, node_size));
   printf("Value:     %u\n", after[node]);
}

void check_heaps_metadata(unique_ptr<u8[]> *meta_before)
{
   for (int h = 0; h < KMALLOC_HEAPS_COUNT && heaps[h]; h++) {

      u8 *meta_ptr = meta_before[h].get();
      kmalloc_heap *heap = heaps[h];

      for (u32 i = 0; i < heap->metadata_size; i++) {

         if (meta_ptr[i] == ((u8*)heap->metadata_nodes)[i])
            continue;

         print_node_info(h, i);
         printf("Exp value: %i\n", meta_ptr[i]);
         FAIL();
      }
   }
}

void kmalloc_chaos_test_sub(default_random_engine &eng,
                            lognormal_distribution<> &dist)
{
   vector<pair<void *, size_t>> allocations;

   for (int i = 0; i < 1000; i++) {

      size_t s = round(dist(eng));

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
      init_kmalloc_for_tests();
   }

   void TearDown() override {
      /* do nothing, for the moment */
   }
};

TEST_F(kmalloc_test, perf_test)
{
   selftest_kmalloc_perf_med();
}

TEST_F(kmalloc_test, glibc_malloc_comparative_perf_test)
{
   mock_kmalloc = true;
   selftest_kmalloc_perf_med();
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

   for (int h = 0; h < KMALLOC_HEAPS_COUNT && heaps[h]; h++) {
      meta_before[h].reset(new u8[heaps[h]->metadata_size]);
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

#define COLOR_RED           "\033[31m"
#define COLOR_YELLOW        "\033[93m"
#define COLOR_BRIGHT_GREEN  "\033[92m"
#define ATTR_BOLD           "\033[1m"
#define ATTR_FAINT          "\033[2m"
#define RESET_ATTRS         "\033[0m"

static void
dump_heap_node_head(block_node n, int w)
{
   printf("%s", ATTR_FAINT);
   printf("+");

   for (int i = 0; i < w-1; i++)
      printf("-");

   printf("%s", RESET_ATTRS);
}

static void
dump_heap_node_head_end(void)
{
   printf(ATTR_FAINT "+\n" RESET_ATTRS);
}

static void
dump_heap_node_tail(block_node n, int w)
{
   dump_heap_node_head(n, w);
}

static void
dump_heap_node_tail_end(void)
{
   dump_heap_node_head_end();
}

static void
dump_heap_node(block_node n, int w)
{
   int i;
   printf(ATTR_FAINT "|" RESET_ATTRS);

   for (i = 0; i < (w-1)/2-1; i++)
      printf(" ");

   printf("%s", COLOR_BRIGHT_GREEN);
   printf("%s", n.allocated ? "A" : "-");
   printf("%s", n.split ? "S" : "-");
   printf("%s", n.full ? "F" : "-");
   printf("%s", RESET_ATTRS);

   for (i += 4; i < w; i++)
      printf(" ");
}

static void
dump_heap_subtree(kmalloc_heap *h, int node, int levels)
{
   int width = (1 << (levels - 1)) * 4;
   int level_width = 1;
   int n = node;

   block_node *nodes = (block_node *)h->metadata_nodes;

   for (int i = 0; i < levels; i++) {

      for (int j = 0; j < level_width; j++)
         dump_heap_node_head(nodes[n + j], width);

      dump_heap_node_head_end();

      for (int j = 0; j < level_width; j++)
         dump_heap_node(nodes[n + j], width);

      printf(ATTR_FAINT "|\n" RESET_ATTRS);

      if (i == levels - 1) {
         for (int j = 0; j < level_width; j++)
            dump_heap_node_head(nodes[n + j], width);

         dump_heap_node_tail_end();
      }

      n = NODE_LEFT(n);
      level_width <<= 1;
      width >>= 1;
   }

   printf("\n");
}


static void check_metadata_row(block_node *nodes, const char *row, int &cn)
{
   const char *p = row;
   assert(*p == '|');
   p++;

   for (; *p; p++) {

      if (*p == ' ')
         continue;

      assert(*p == '-' || *p == 'A' || *p == 'S' || *p == 'F');

      u8 val = 0;

      for (; *p != ' ' && *p != '|'; p++) {

         assert(*p);

         switch (*p) {
            case 'A':
               val |= FL_NODE_ALLOCATED;
               break;

            case 'S':
               val |= FL_NODE_SPLIT;
               break;

            case 'F':
               val |= FL_NODE_FULL;
               break;
         }
      }

      EXPECT_EQ(nodes[cn].raw, val) << "node #" << cn;
      cn++;

      while (*p == ' ')
         p++;

      assert(*p == '|');
   }

}

static void
check_metadata(block_node *nodes, vector<const char *> expected_vec)
{
   int cn = 0;

   for (const char *row: expected_vec) {

      if (*row == '+')
         continue;

      check_metadata_row(nodes, row, cn);
   }
}

TEST_F(kmalloc_test, split_block)
{
   void *ptr;
   size_t s;

   kmalloc_heap h;
   kmalloc_create_heap(&h,
                       MB,                           /* vaddr */
                       KMALLOC_MIN_HEAP_SIZE,        /* heap size */
                       KMALLOC_MIN_HEAP_SIZE / 16,   /* min block size */
                       0,    /* alloc block size: 0 because linear_mapping=1 */
                       true, /* linear mapping */
                       NULL, NULL, NULL);

   block_node *nodes = (block_node *)h.metadata_nodes;

   s = h.size / 2;
   ptr = per_heap_kmalloc(&h, &s, 0);
   ASSERT_TRUE(ptr != NULL);

   printf("\nAfter alloc of heap_size/2:\n");
   dump_heap_subtree(&h, 0, 5);

   check_metadata(nodes, {
      "+---------------------------------------------------------------+",
      "|                              -S-                              |",
      "+-------------------------------+-------------------------------+",
      "|              --F              |              ---              |",
      "+---------------+---------------+---------------+---------------+",
      "|      ---      |      ---      |      ---      |      ---      |",
      "+-------+-------+-------+-------+-------+-------+-------+-------+",
      "|  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+",
      "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+"
   });


   EXPECT_TRUE(nodes[0].raw == FL_NODE_SPLIT);
   EXPECT_TRUE(nodes[1].raw == FL_NODE_FULL);


   internal_kmalloc_split_block(&h, ptr, s, h.min_block_size);

   printf("After split_block:\n");
   dump_heap_subtree(&h, 0, 5);

   check_metadata(nodes, {
      "+---------------------------------------------------------------+",
      "|                              -S-                              |",
      "+-------------------------------+-------------------------------+",
      "|              -SF              |              ---              |",
      "+---------------+---------------+---------------+---------------+",
      "|      -SF      |      -SF      |      ---      |      ---      |",
      "+-------+-------+-------+-------+-------+-------+-------+-------+",
      "|  -SF  |  -SF  |  -SF  |  -SF  |  ---  |  ---  |  ---  |  ---  |",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+",
      "|--F|--F|--F|--F|--F|--F|--F|--F|---|---|---|---|---|---|---|---|",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+"
   });

   printf("After kfree leaf node #3:\n");

   size_t actual_size = h.min_block_size;

   per_heap_kfree(&h,
                  (void *)(h.vaddr + h.min_block_size * 3),
                  &actual_size,
                  0);

   ASSERT_EQ(actual_size, h.min_block_size);

   dump_heap_subtree(&h, 0, 5);

   check_metadata(nodes, {
      "+---------------------------------------------------------------+",
      "|                              -S-                              |",
      "+-------------------------------+-------------------------------+",
      "|              -S-              |              ---              |",
      "+---------------+---------------+---------------+---------------+",
      "|      -S-      |      -SF      |      ---      |      ---      |",
      "+-------+-------+-------+-------+-------+-------+-------+-------+",
      "|  -SF  |  -S-  |  -SF  |  -SF  |  ---  |  ---  |  ---  |  ---  |",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+",
      "|--F|--F|--F|---|--F|--F|--F|--F|---|---|---|---|---|---|---|---|",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+"
   });


   kmalloc_destroy_heap(&h);
}

TEST_F(kmalloc_test, coalesce_block)
{
   void *ptr;
   size_t s;

   kmalloc_heap h;
   kmalloc_create_heap(&h,
                       MB,                           /* vaddr */
                       KMALLOC_MIN_HEAP_SIZE,        /* heap size */
                       KMALLOC_MIN_HEAP_SIZE / 16,   /* min block size */
                       0,    /* alloc block size: 0 because linear_mapping=1 */
                       true, /* linear mapping */
                       NULL, NULL, NULL);

   block_node *nodes = (block_node *)h.metadata_nodes;

   s = h.size / 2;
   ptr = per_heap_kmalloc(&h, &s, 0);
   ASSERT_TRUE(ptr != NULL);

   internal_kmalloc_split_block(&h, ptr, s, h.min_block_size);

   printf("After split_block:\n");
   dump_heap_subtree(&h, 0, 5);

   check_metadata(nodes, {
      "+---------------------------------------------------------------+",
      "|                              -S-                              |",
      "+-------------------------------+-------------------------------+",
      "|              -SF              |              ---              |",
      "+---------------+---------------+---------------+---------------+",
      "|      -SF      |      -SF      |      ---      |      ---      |",
      "+-------+-------+-------+-------+-------+-------+-------+-------+",
      "|  -SF  |  -SF  |  -SF  |  -SF  |  ---  |  ---  |  ---  |  ---  |",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+",
      "|--F|--F|--F|--F|--F|--F|--F|--F|---|---|---|---|---|---|---|---|",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+"
   });

   internal_kmalloc_coalesce_block(&h,
                                   (void *)(h.vaddr + h.size / 4),
                                   h.size / 4);

   printf("After coalesce node #4:\n");
   dump_heap_subtree(&h, 0, 5);

   check_metadata(nodes, {
      "+---------------------------------------------------------------+",
      "|                              -S-                              |",
      "+-------------------------------+-------------------------------+",
      "|              -SF              |              ---              |",
      "+---------------+---------------+---------------+---------------+",
      "|      -SF      |      --F      |      ---      |      ---      |",
      "+-------+-------+-------+-------+-------+-------+-------+-------+",
      "|  -SF  |  -SF  |  ---  |  ---  |  ---  |  ---  |  ---  |  ---  |",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+",
      "|--F|--F|--F|--F|---|---|---|---|---|---|---|---|---|---|---|---|",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+"
   });

   kmalloc_destroy_heap(&h);
}

static bool fake_alloc_and_map_func(uptr vaddr, int page_count) { return true; }
static void fake_free_and_map_func(uptr vaddr, int page_count) { }

TEST_F(kmalloc_test, multi_step_alloc)
{
   void *ptr;
   size_t s;

   kmalloc_heap h;
   kmalloc_create_heap(&h,
                       MB,                           /* vaddr */
                       KMALLOC_MIN_HEAP_SIZE,        /* heap size */
                       KMALLOC_MIN_HEAP_SIZE / 16,   /* min block size */
                       KMALLOC_MIN_HEAP_SIZE / 8,    /* alloc block size */
                       false,                        /* linear mapping */
                       NULL,                         /* metadata_nodes */
                       fake_alloc_and_map_func,
                       fake_free_and_map_func);

   block_node *nodes = (block_node *)h.metadata_nodes;

   s = 15 * h.min_block_size;
   ptr = per_heap_kmalloc(&h, &s, KMALLOC_FL_MULTI_STEP);

   EXPECT_EQ(s, 15 * h.min_block_size);
   EXPECT_EQ(ptr, (void *)h.vaddr);

   dump_heap_subtree(&h, 0, 5);

   check_metadata(nodes, {
      "+---------------------------------------------------------------+",
      "|                              -S-                              |",
      "+-------------------------------+-------------------------------+",
      "|              --F              |              -S-              |",
      "+---------------+---------------+---------------+---------------+",
      "|      ---      |      ---      |      --F      |      -S-      |",
      "+-------+-------+-------+-------+-------+-------+-------+-------+",
      "|  A-F  |  A-F  |  A-F  |  A-F  |  A-F  |  A-F  |  A-F  |  AS-  |",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+",
      "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|--F|---|",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+"
   });

   kmalloc_destroy_heap(&h);
}


TEST_F(kmalloc_test, multi_step_alloc2)
{
   void *ptr;
   size_t s;

   kmalloc_heap h;
   kmalloc_create_heap(&h,
                       MB,                           /* vaddr */
                       KMALLOC_MIN_HEAP_SIZE,        /* heap size */
                       KMALLOC_MIN_HEAP_SIZE / 16,   /* min block size */
                       KMALLOC_MIN_HEAP_SIZE / 8,    /* alloc block size */
                       false,                        /* linear mapping */
                       NULL,                         /* metadata_nodes */
                       fake_alloc_and_map_func,
                       fake_free_and_map_func);

   block_node *nodes = (block_node *)h.metadata_nodes;

   s = 11 * h.min_block_size;
   ptr = per_heap_kmalloc(&h, &s, KMALLOC_FL_MULTI_STEP);

   EXPECT_EQ(s, 11 * h.min_block_size);
   EXPECT_EQ(ptr, (void *)h.vaddr);

   dump_heap_subtree(&h, 0, 5);

   check_metadata(nodes, {
      "+---------------------------------------------------------------+",
      "|                              -S-                              |",
      "+-------------------------------+-------------------------------+",
      "|              --F              |              -S-              |",
      "+---------------+---------------+---------------+---------------+",
      "|      ---      |      ---      |      -S-      |      ---      |",
      "+-------+-------+-------+-------+-------+-------+-------+-------+",
      "|  A-F  |  A-F  |  A-F  |  A-F  |  A-F  |  AS-  |  ---  |  ---  |",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+",
      "|---|---|---|---|---|---|---|---|---|---|--F|---|---|---|---|---|",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+"
   });

   kmalloc_destroy_heap(&h);
}

TEST_F(kmalloc_test, multi_step_and_split)
{
   void *ptr;
   size_t s;

   kmalloc_heap h;
   kmalloc_create_heap(&h,
                       MB,                           /* vaddr */
                       KMALLOC_MIN_HEAP_SIZE,        /* heap size */
                       KMALLOC_MIN_HEAP_SIZE / 16,   /* min block size */
                       KMALLOC_MIN_HEAP_SIZE / 8,    /* alloc block size */
                       false,                        /* linear mapping */
                       NULL,                         /* metadata_nodes */
                       fake_alloc_and_map_func,
                       fake_free_and_map_func);

   block_node *nodes = (block_node *)h.metadata_nodes;

   s = 15 * h.min_block_size;
   ptr = per_heap_kmalloc(&h, &s, KMALLOC_FL_MULTI_STEP | h.min_block_size);

   EXPECT_EQ(s, 15 * h.min_block_size);
   EXPECT_EQ(ptr, (void *)h.vaddr);

   dump_heap_subtree(&h, 0, 5);

   check_metadata(nodes, {
      "+---------------------------------------------------------------+",
      "|                              -S-                              |",
      "+-------------------------------+-------------------------------+",
      "|              -SF              |              -S-              |",
      "+---------------+---------------+---------------+---------------+",
      "|      -SF      |      -SF      |      -SF      |      -S-      |",
      "+-------+-------+-------+-------+-------+-------+-------+-------+",
      "|  ASF  |  ASF  |  ASF  |  ASF  |  ASF  |  ASF  |  ASF  |  AS-  |",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+",
      "|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|---|",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+"
   });

   kmalloc_destroy_heap(&h);
}

TEST_F(kmalloc_test, multi_step_free)
{
   void *ptr;
   size_t s;

   kmalloc_heap h;
   kmalloc_create_heap(&h,
                       MB,                           /* vaddr */
                       KMALLOC_MIN_HEAP_SIZE,        /* heap size */
                       KMALLOC_MIN_HEAP_SIZE / 16,   /* min block size */
                       KMALLOC_MIN_HEAP_SIZE / 8,    /* alloc block size */
                       false,                        /* linear mapping */
                       NULL,                         /* metadata_nodes */
                       fake_alloc_and_map_func,
                       fake_free_and_map_func);

   block_node *nodes = (block_node *)h.metadata_nodes;

   s = 15 * h.min_block_size;
   ptr = per_heap_kmalloc(&h, &s, KMALLOC_FL_MULTI_STEP | h.min_block_size);

   EXPECT_EQ(s, 15 * h.min_block_size);
   EXPECT_EQ(ptr, (void *)h.vaddr);

   dump_heap_subtree(&h, 0, 5);

   check_metadata(nodes, {
      "+---------------------------------------------------------------+",
      "|                              -S-                              |",
      "+-------------------------------+-------------------------------+",
      "|              -SF              |              -S-              |",
      "+---------------+---------------+---------------+---------------+",
      "|      -SF      |      -SF      |      -SF      |      -S-      |",
      "+-------+-------+-------+-------+-------+-------+-------+-------+",
      "|  ASF  |  ASF  |  ASF  |  ASF  |  ASF  |  ASF  |  ASF  |  AS-  |",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+",
      "|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|---|",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+"
   });

   size_t actual_size = h.min_block_size * 7;
   per_heap_kfree(&h,
                  (void *)((uptr)ptr + h.min_block_size * 4),
                  &actual_size,
                  KFREE_FL_ALLOW_SPLIT | KFREE_FL_MULTI_STEP);

   ASSERT_EQ(actual_size, h.min_block_size * 7);

   printf("After multi-step free:\n");
   dump_heap_subtree(&h, 0, 5);

   check_metadata(nodes, {
      "+---------------------------------------------------------------+",
      "|                              -S-                              |",
      "+-------------------------------+-------------------------------+",
      "|              -S-              |              -S-              |",
      "+---------------+---------------+---------------+---------------+",
      "|      -SF      |      ---      |      -S-      |      -S-      |",
      "+-------+-------+-------+-------+-------+-------+-------+-------+",
      "|  ASF  |  ASF  |  ---  |  ---  |  ---  |  AS-  |  ASF  |  AS-  |",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+",
      "|--F|--F|--F|--F|---|---|---|---|---|---|---|--F|--F|--F|--F|---|",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+"
   });

   kmalloc_destroy_heap(&h);
}


TEST_F(kmalloc_test, partial_free)
{
   void *ptr;
   size_t s;

   kmalloc_heap h;
   kmalloc_create_heap(&h,
                       MB,                           /* vaddr */
                       KMALLOC_MIN_HEAP_SIZE,        /* heap size */
                       KMALLOC_MIN_HEAP_SIZE / 16,   /* min block size */
                       KMALLOC_MIN_HEAP_SIZE / 8,    /* alloc block size */
                       false,                        /* linear mapping */
                       NULL,                         /* metadata_nodes */
                       fake_alloc_and_map_func,
                       fake_free_and_map_func);

   block_node *nodes = (block_node *)h.metadata_nodes;

   s = h.size;
   ptr = per_heap_kmalloc(&h, &s, h.min_block_size);

   EXPECT_EQ(s, 16 * h.min_block_size);
   EXPECT_EQ(ptr, (void *)h.vaddr);

   dump_heap_subtree(&h, 0, 5);

   check_metadata(nodes, {
      "+---------------------------------------------------------------+",
      "|                              -SF                              |",
      "+-------------------------------+-------------------------------+",
      "|              -SF              |              -SF              |",
      "+---------------+---------------+---------------+---------------+",
      "|      -SF      |      -SF      |      -SF      |      -SF      |",
      "+-------+-------+-------+-------+-------+-------+-------+-------+",
      "|  ASF  |  ASF  |  ASF  |  ASF  |  ASF  |  ASF  |  ASF  |  ASF  |",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+",
      "|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+"
   });

   EXPECT_EQ(h.mem_allocated, h.size);

   s = h.min_block_size;
   ptr = (void *)(h.vaddr + 2 * h.min_block_size);
   per_heap_kfree(&h, ptr, &s, 0);

   dump_heap_subtree(&h, 0, 5);

   check_metadata(nodes, {
      "+---------------------------------------------------------------+",
      "|                              -S-                              |",
      "+-------------------------------+-------------------------------+",
      "|              -S-              |              -SF              |",
      "+---------------+---------------+---------------+---------------+",
      "|      -S-      |      -SF      |      -SF      |      -SF      |",
      "+-------+-------+-------+-------+-------+-------+-------+-------+",
      "|  ASF  |  AS-  |  ASF  |  ASF  |  ASF  |  ASF  |  ASF  |  ASF  |",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+",
      "|--F|--F|---|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+"
   });

   EXPECT_EQ(h.mem_allocated, h.size - h.min_block_size);

   s = h.min_block_size * 2;
   ptr = (void *)(h.vaddr + 2 * h.min_block_size);
   per_heap_kfree(&h, ptr, &s, KFREE_FL_ALLOW_SPLIT);

   dump_heap_subtree(&h, 0, 5);

   check_metadata(nodes, {
      "+---------------------------------------------------------------+",
      "|                              -S-                              |",
      "+-------------------------------+-------------------------------+",
      "|              -S-              |              -SF              |",
      "+---------------+---------------+---------------+---------------+",
      "|      -S-      |      -SF      |      -SF      |      -SF      |",
      "+-------+-------+-------+-------+-------+-------+-------+-------+",
      "|  ASF  |  ---  |  ASF  |  ASF  |  ASF  |  ASF  |  ASF  |  ASF  |",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+",
      "|--F|--F|---|---|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|--F|",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+"
   });

   EXPECT_EQ(h.mem_allocated, h.size - h.min_block_size * 2);

   s = h.min_block_size * 8;
   ptr = (void *)(h.vaddr);
   per_heap_kfree(&h, ptr, &s, KFREE_FL_ALLOW_SPLIT);

   dump_heap_subtree(&h, 0, 5);

   check_metadata(nodes, {
      "+---------------------------------------------------------------+",
      "|                              -S-                              |",
      "+-------------------------------+-------------------------------+",
      "|              ---              |              -SF              |",
      "+---------------+---------------+---------------+---------------+",
      "|      ---      |      ---      |      -SF      |      -SF      |",
      "+-------+-------+-------+-------+-------+-------+-------+-------+",
      "|  ---  |  ---  |  ---  |  ---  |  ASF  |  ASF  |  ASF  |  ASF  |",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+",
      "|---|---|---|---|---|---|---|---|--F|--F|--F|--F|--F|--F|--F|--F|",
      "+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+"
   });

   EXPECT_EQ(h.mem_allocated, h.size - h.min_block_size * 8);

   kmalloc_destroy_heap(&h);
}
