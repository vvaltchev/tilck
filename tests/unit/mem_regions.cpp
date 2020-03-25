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

   extern struct mem_region mem_regions[MAX_MEM_REGIONS];
   extern int mem_regions_count;

   void append_mem_region(struct mem_region r);
   void fix_mem_regions(void);
   void align_mem_regions_to_page_boundary(void);
   void sort_mem_regions(void);
   void merge_adj_mem_regions(void);
   void handle_overlapping_regions(void);
}

struct test_mem_reg {

   const u64 start;
   const u64 end;
   const u32 type;

   test_mem_reg(u64 start_reg, u64 end_reg, u32 type_reg)
      : start(start_reg)
      , end(end_reg)
      , type(type_reg)
   { }

   test_mem_reg(const mem_region &r)
      : start(r.addr)
      , end(r.addr + r.len)
      , type(r.type)
   { }

   operator mem_region() const {
      return mem_region{start, end - start, type, 0 /* extra */};
   }

   bool operator==(const test_mem_reg &rhs) const {
      return start == rhs.start && end == rhs.end && type == rhs.type;
   }
};

std::ostream& operator<<(std::ostream& os, const test_mem_reg& reg) {

  return os << "reg("
            << hex << "0x"
            << reg.start
            << ", " << "0x"
            << reg.end
            << ", "
            << reg.type
            << ")";
}

static void reset_mem_regions(void)
{
   memset(mem_regions, 0, sizeof(mem_regions));
   mem_regions_count = 0;
}

class mem_regions_test :
   public TestWithParam<
      pair<
         vector<test_mem_reg>, /* input regions */
         vector<test_mem_reg>  /* expected regions */
      >
   >
{

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
   struct mem_region saved_mem_regions[MAX_MEM_REGIONS];
   int saved_mem_regions_count;
};

TEST_P(mem_regions_test, check)
{
   const auto &p = GetParam();
   const auto &input = p.first;
   const auto &expected = p.second;

   reset_mem_regions();
   ASSERT_EQ(mem_regions_count, 0);

   for (const auto &e: input) {
      append_mem_region(e);
   }

   fix_mem_regions();
   ASSERT_EQ(mem_regions_count, (int)expected.size());

   for (int i = 0; i < mem_regions_count; i++) {
      ASSERT_EQ(test_mem_reg(mem_regions[i]), expected[i]);
   }
}

static inline pair<vector<test_mem_reg>, vector<test_mem_reg>>
make_regions_list_pair(const vector<test_mem_reg> &a,
                       const vector<test_mem_reg> &b)
{
   return make_pair(a, b);
}

INSTANTIATE_TEST_CASE_P(

   do_nothing,
   mem_regions_test,

   Values(

      /* empty set */
      make_regions_list_pair( {}, {} ),

      /* just one region */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x1000, 1}
         },
         {
            test_mem_reg{0x0000, 0x1000, 1}
         }
      ),

      /* two adjacent regions of a different type */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x1000, 1},
            test_mem_reg{0x1000, 0x2000, 2},
         },
         {
            test_mem_reg{0x0000, 0x1000, 1},
            test_mem_reg{0x1000, 0x2000, 2},
         }
      ),

      /* just two separated regions */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x1000, 1},
            test_mem_reg{0x2000, 0x3000, 2},
         },
         {
            test_mem_reg{0x0000, 0x1000, 1},
            test_mem_reg{0x2000, 0x3000, 2},
         }
      )
   )
);

INSTANTIATE_TEST_CASE_P(

   reorder,
   mem_regions_test,

   Values(

      make_regions_list_pair(
         {
            test_mem_reg{0x1000, 0x2000, 2},
            test_mem_reg{0x0000, 0x1000, 1},
         },
         {
            test_mem_reg{0x0000, 0x1000, 1},
            test_mem_reg{0x1000, 0x2000, 2},
         }
      )
   )
);

INSTANTIATE_TEST_CASE_P(

   align_to_4k,
   mem_regions_test,

   Values(

      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x1, 1},
            test_mem_reg{0x2fff, 0x3fff, 2},
            test_mem_reg{0x5500, 0x5600, 1},
         },
         {
            test_mem_reg{0x0000, 0x1000, 1},
            test_mem_reg{0x2000, 0x4000, 2},
            test_mem_reg{0x5000, 0x6000, 1},
         }
      )
   )
);

INSTANTIATE_TEST_CASE_P(

   merge,
   mem_regions_test,

   Values(

      /* simple merge */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x1000, 1},
            test_mem_reg{0x1000, 0x2000, 1},
         },
         {
            test_mem_reg{0x0000, 0x2000, 1},
         }
      ),

      /* merge after align to 4k [1/2] */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x0fff, 1},
            test_mem_reg{0x1000, 0x2000, 1},
         },
         {
            test_mem_reg{0x0000, 0x2000, 1},
         }
      ),

      /* merge after align to 4k [2/2] */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x1000, 1},
            test_mem_reg{0x1001, 0x2000, 1},
         },
         {
            test_mem_reg{0x0000, 0x2000, 1},
         }
      ),

      /* merge 3 regions */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x1000, 1},
            test_mem_reg{0x1000, 0x2000, 1},
            test_mem_reg{0x2000, 0x3000, 1},
         },
         {
            test_mem_reg{0x0000, 0x3000, 1},
         }
      )
   )
);

INSTANTIATE_TEST_CASE_P(

   full_overlap,
   mem_regions_test,

   Values(

      /*
       *  Full overlap (reg2.type > reg1.type)
       *
       *  +---------------------------------+
       *  |            region 1             |
       *  +---------------------------------+
       *            +---------------+
       *            |   region 2    |
       *            +---------------+
       */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x8000, 1},
            test_mem_reg{0x3000, 0x4000, 2},
         },
         {
            test_mem_reg{0x0000, 0x3000, 1},
            test_mem_reg{0x3000, 0x4000, 2},
            test_mem_reg{0x4000, 0x8000, 1},
         }
      ),

      /*
       *  Full overlap (reg2.type < reg1.type)
       *
       *  +---------------------------------+
       *  |            region 1             |
       *  +---------------------------------+
       *            +---------------+
       *            |   region 2    |
       *            +---------------+
       */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x8000, 2},
            test_mem_reg{0x3000, 0x4000, 1},
         },
         {
            test_mem_reg{0x0000, 0x8000, 2},
         }
      ),

      /*
       * Full overlap corner case (2a, reg2.type < reg1.type)
       *
       *  +---------------------------------+
       *  |            region 1             |
       *  +---------------------------------+
       *  +---------------------------------+
       *  |            region 2             |
       *  +---------------------------------+
      */
      make_regions_list_pair(
         {
            test_mem_reg{0x1000, 0x2000, 1},
            test_mem_reg{0x1000, 0x2000, 2},
         },
         {
            test_mem_reg{0x1000, 0x2000, 2},
         }
      ),

      /*
       * Full overlap corner case (2b, reg2.type < reg1.type)
       *
       *  +---------------------------------+
       *  |            region 1             |
       *  +---------------------------------+
       *  +---------------+
       *  |   region 2    |
       *  +---------------+
      */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x4000, 1},
            test_mem_reg{0x0000, 0x2000, 2},
         },
         {
            test_mem_reg{0x0000, 0x2000, 2},
            test_mem_reg{0x2000, 0x4000, 1},
         }
      ),

      /*
       * Full overlap corner case (2b, reg2.type > reg1.type)
       *
       *  +---------------------------------+
       *  |            region 1             |
       *  +---------------------------------+
       *  +---------------+
       *  |   region 2    |
       *  +---------------+
      */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x4000, 2},
            test_mem_reg{0x0000, 0x2000, 1},
         },
         {
            test_mem_reg{0x0000, 0x4000, 2},
         }
      ),

      /*
       * Full overlap corner case (2c, reg2.type < reg1.type)
       *
       *  +---------------------------------+
       *  |            region 1             |
       *  +---------------------------------+
       *                    +---------------+
       *                    |   region 2    |
       *                    +---------------+
      */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x4000, 1},
            test_mem_reg{0x2000, 0x4000, 2},
         },
         {
            test_mem_reg{0x0000, 0x2000, 1},
            test_mem_reg{0x2000, 0x4000, 2},
         }
      ),

      /*
       * Full overlap corner case (2c, reg2.type > reg1.type)
       *
       *  +---------------------------------+
       *  |            region 1             |
       *  +---------------------------------+
       *                    +---------------+
       *                    |   region 2    |
       *                    +---------------+
      */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x4000, 2},
            test_mem_reg{0x2000, 0x4000, 1},
         },
         {
            test_mem_reg{0x0000, 0x4000, 2},
         }
      )
   )
);


INSTANTIATE_TEST_CASE_P(

   full_overlap_rev,
   mem_regions_test,

   Values(

      /*
       *  Full overlap (reg2.type > reg1.type)
       *
       *            +---------------+
       *            |   region 2    |
       *            +---------------+
       *  +---------------------------------+
       *  |            region 1             |
       *  +---------------------------------+
       */
      make_regions_list_pair(
         {
            test_mem_reg{0x3000, 0x4000, 2},
            test_mem_reg{0x0000, 0x8000, 1},
         },
         {
            test_mem_reg{0x0000, 0x3000, 1},
            test_mem_reg{0x3000, 0x4000, 2},
            test_mem_reg{0x4000, 0x8000, 1},
         }
      ),

      /*
       *  Full overlap (reg2.type < reg1.type)
       *
       *            +---------------+
       *            |   region 2    |
       *            +---------------+
       *  +---------------------------------+
       *  |            region 1             |
       *  +---------------------------------+
       */
      make_regions_list_pair(
         {
            test_mem_reg{0x3000, 0x4000, 1},
            test_mem_reg{0x0000, 0x8000, 2},
         },
         {
            test_mem_reg{0x0000, 0x8000, 2},
         }
      ),

      /*
       * Full overlap corner case (2b, reg2.type < reg1.type)
       *
       *  +---------------+
       *  |   region 2    |
       *  +---------------+
       *  +---------------------------------+
       *  |            region 1             |
       *  +---------------------------------+
      */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x2000, 2},
            test_mem_reg{0x0000, 0x4000, 1},
         },
         {
            test_mem_reg{0x0000, 0x2000, 2},
            test_mem_reg{0x2000, 0x4000, 1},
         }
      ),

      /*
       * Full overlap corner case (2b, reg2.type > reg1.type)
       *
       *  +---------------+
       *  |   region 2    |
       *  +---------------+
       *  +---------------------------------+
       *  |            region 1             |
       *  +---------------------------------+
      */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x2000, 1},
            test_mem_reg{0x0000, 0x4000, 2},
         },
         {
            test_mem_reg{0x0000, 0x4000, 2},
         }
      ),

      /*
       * Full overlap corner case (2c, reg2.type < reg1.type)
       *
       *                    +---------------+
       *                    |   region 2    |
       *                    +---------------+
       *  +---------------------------------+
       *  |            region 1             |
       *  +---------------------------------+
      */
      make_regions_list_pair(
         {
            test_mem_reg{0x2000, 0x4000, 2},
            test_mem_reg{0x0000, 0x4000, 1},
         },
         {
            test_mem_reg{0x0000, 0x2000, 1},
            test_mem_reg{0x2000, 0x4000, 2},
         }
      ),

      /*
       * Full overlap corner case (2c, reg2.type > reg1.type)
       *
       *                    +---------------+
       *                    |   region 2    |
       *                    +---------------+
       *  +---------------------------------+
       *  |            region 1             |
       *  +---------------------------------+
      */
      make_regions_list_pair(
         {
            test_mem_reg{0x2000, 0x4000, 1},
            test_mem_reg{0x0000, 0x4000, 2},
         },
         {
            test_mem_reg{0x0000, 0x4000, 2},
         }
      )
   )
);

INSTANTIATE_TEST_CASE_P(

   partial_overlap,
   mem_regions_test,

   Values(

      /*
       *  Partial overlap (reg2.type > reg1.type)
       *
       *  +---------------------------------+
       *  |            region 1             |
       *  +---------------------------------+
       *                    +---------------------------+
       *                    |          region 2         |
       *                    +---------------------------+
       */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x4000, 1},
            test_mem_reg{0x2000, 0x8000, 2},
         },
         {
            test_mem_reg{0x0000, 0x2000, 1},
            test_mem_reg{0x2000, 0x8000, 2},
         }
      ),

      /*
       *  Partial overlap (reg1.type > reg2.type)
       *
       *  +---------------------------------+
       *  |            region 1             |
       *  +---------------------------------+
       *                    +---------------------------+
       *                    |          region 2         |
       *                    +---------------------------+
       */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x4000, 2},
            test_mem_reg{0x2000, 0x8000, 1},
         },
         {
            test_mem_reg{0x0000, 0x4000, 2},
            test_mem_reg{0x4000, 0x8000, 1},
         }
      ),

      /*
       *  Partial overlap (3a, reg2.type > reg1.type)
       *
       *  +----------------------------+
       *  |          region 1          |
       *  +----------------------------+
       *  +--------------------------------------------+
       *  |                  region 2                  |
       *  +--------------------------------------------+
       */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x4000, 1},
            test_mem_reg{0x0000, 0x8000, 2},
         },
         {
            test_mem_reg{0x0000, 0x8000, 2},
         }
      ),

      /*
       *  Partial overlap (3a, reg1.type > reg2.type)
       *
       *  +----------------------------+
       *  |          region 1          |
       *  +----------------------------+
       *  +--------------------------------------------+
       *  |                  region 2                  |
       *  +--------------------------------------------+
       */
      make_regions_list_pair(
         {
            test_mem_reg{0x0000, 0x4000, 2},
            test_mem_reg{0x0000, 0x8000, 1},
         },
         {
            test_mem_reg{0x0000, 0x4000, 2},
            test_mem_reg{0x4000, 0x8000, 1},
         }
      )
   )
);
