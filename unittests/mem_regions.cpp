
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
}

void reset_mem_regions(void)
{
   memset(mem_regions, 0, sizeof(mem_regions));
   mem_regions_count = 0;
}

TEST(mem_regions, test1)
{
   reset_mem_regions();

   append_mem_region(memory_region_t{
      0x1d8000,
      0x492a00 - 0x1d8000,
      2,
      MEM_REG_EXTRA_RAMDISK
   });

   append_mem_region(memory_region_t{
      0x0,
      0x10000 - 0x0,
      2,
      MEM_REG_EXTRA_LOWMEM
   });

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
      0x100000,
      0x649d3000 - 0x100000,
      1,
      0
   });

   append_mem_region(memory_region_t{
      0x649d3000,
      0x649d4000 - 0x649d3000,
      4,
      0
   });

   append_mem_region(memory_region_t{
      0x649d4000,
      0x70569000 - 0x649d4000,
      2,
      0
   });

   append_mem_region(memory_region_t{
      0x70569000,
      0x705ad000 - 0x70569000,
      3,
      0
   });

   append_mem_region(memory_region_t{
      0x705ad000,
      0x7ac1f000 - 0x705ad000,
      4,
      0
   });

   append_mem_region(memory_region_t{
      0x7ac1f000,
      0x7b600000 - 0x7ac1f000,
      2,
      0
   });

   append_mem_region(memory_region_t{
      0x100000000,
      0x47e800000 - 0x100000000,
      1,
      0
   });

   // append_mem_region(memory_region_t{
   //    0x7b600000,
   //    0x7f800000 - 0x7b600000,
   //    2,
   //    0
   // });

   // append_mem_region(memory_region_t{
   //    0xe0000000,
   //    0xf0000000 - 0xe0000000,
   //    2,
   //    0
   // });

   // append_mem_region(memory_region_t{
   //    0xfe000000,
   //    0xfe011000 - 0xfe000000,
   //    2,
   //    0
   // });

   // append_mem_region(memory_region_t{
   //    0xfec00000,
   //    0xfec01000 - 0xfec00000,
   //    2,
   //    0
   // });

   // append_mem_region(memory_region_t{
   //    0xfee00000,
   //    0xfee01000 - 0xfee00000,
   //    2,
   //    0
   // });

   // append_mem_region(memory_region_t{
   //    0x100000,
   //    0x12b955 - 0x100000,
   //    2,
   //    MEM_REG_EXTRA_KERNEL
   // });

   // append_mem_region(memory_region_t{
   //    0x12c000,
   //    0x1a9040 - 0x12c000,
   //    2,
   //    MEM_REG_EXTRA_KERNEL
   // });

   // append_mem_region(memory_region_t{
   //    0x1aa000,
   //    0x1b6000 - 0x1aa000,
   //    2,
   //    MEM_REG_EXTRA_KERNEL
   // });


   dump_system_memory_map();

   fix_mem_regions();

   dump_system_memory_map();
}
