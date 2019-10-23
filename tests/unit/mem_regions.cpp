/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

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

using namespace std;
using namespace testing;

extern "C" {
   #include <tilck/kernel/system_mmap.h>
   void append_mem_region(struct memory_region_t r);
   void fix_mem_regions(void);
   void align_mem_regions_to_page_boundary(void);
   void sort_mem_regions(void);
   void merge_adj_mem_regions(void);
   void handle_overlapping_regions(void);
}

class mem_reg2 {

   public:

   u64 start;
   u64 end;
   u32 type;
   u32 extra;

   operator memory_region_t() {
      return memory_region_t{start, end - start, type, extra};
   }
};

void reset_mem_regions(void)
{
   memset(mem_regions, 0, sizeof(mem_regions));
   mem_regions_count = 0;
}

class mem_regions_test : public Test {

public:

   void SetUp() override {
      memcpy(saved_mem_regions, mem_regions, sizeof(mem_regions));
      saved_mem_regions_count = mem_regions_count;
   }

   void TearDown() override {
      memcpy(mem_regions, saved_mem_regions, sizeof(mem_regions));
      mem_regions_count = saved_mem_regions_count;
   }

private:
   struct memory_region_t saved_mem_regions[MAX_MEM_REGIONS];
   int saved_mem_regions_count;
};


TEST_F(mem_regions_test, large_mem)
{
   append_mem_region(mem_reg2{0x00000, 0x9dc00, 1, 0});
   append_mem_region(mem_reg2{0x9dc00, 0xa0000, 2, 0});
   append_mem_region(mem_reg2{0xe0000, 0x100000, 2, 0});
   append_mem_region(mem_reg2{0x100000000ull, 0x47e800000ull, 1, 0});

   // dump_memory_map("Original memory map", mem_regions, mem_regions_count);

   fix_mem_regions();

   // dump_memory_map("Normalized memory map", mem_regions, mem_regions_count);

   // TODO: add expect, assert statements to this test
}
