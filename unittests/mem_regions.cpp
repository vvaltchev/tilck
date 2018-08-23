
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

extern "C" {
   #include <tilck/kernel/system_mmap.h>
   void append_mem_region(memory_region_t r);
   void fix_mem_regions(void);
   void align_mem_regions_to_page_boundary(void);
   void sort_mem_regions(void);
   void merge_adj_mem_regions(void);
   void handle_overlapping_regions(void);
}

void reset_mem_regions(void)
{
   memset(mem_regions, 0, sizeof(mem_regions));
   mem_regions_count = 0;
}

TEST(mem_regions, large_mem)
{
   reset_mem_regions();

   append_mem_region(memory_region_t{
      0x0,
      0x9dc00 - 0x0,
      1,
      0
   });

   append_mem_region(memory_region_t{
      0x9dc00,
      0xa0000 - 0x9dc00,
      2,
      0
   });

   append_mem_region(memory_region_t{
      0xe0000,
      0x100000 - 0xe0000,
      2,
      0
   });

   append_mem_region(memory_region_t{
      0x100000000,
      0x47e800000 - 0x100000000,
      1,
      0
   });

   dump_memory_map("Original memory map", mem_regions, mem_regions_count);

   //fix_mem_regions();

   align_mem_regions_to_page_boundary();
   sort_mem_regions();
   merge_adj_mem_regions();
   handle_overlapping_regions();

   dump_memory_map("Normalized memory map", mem_regions, mem_regions_count);
}
