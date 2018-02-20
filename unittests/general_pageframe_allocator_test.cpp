
#include <gtest/gtest.h>
#include "kernel_init_funcs.h"

extern "C" {
   #include <common_defs.h>
   #include <paging.h>
   #include <self_tests/self_tests.h>
   uptr paging_alloc_pageframe();
   void paging_free_pageframe(uptr address);
}

#define RESERVED_MB (INITIAL_MB_RESERVED + MB_RESERVED_FOR_PAGING)
#define POTENTIAL_AVAIL_MEM_MB (MAX_MEM_SIZE_IN_MB - RESERVED_MB)

static const uptr avail_pages = POTENTIAL_AVAIL_MEM_MB * MB / PAGE_SIZE;

TEST(alloc_pageframe, seq_alloc)
{
   init_pageframe_allocator();

   for (uptr i = 0; i < (POTENTIAL_AVAIL_MEM_MB * MB / PAGE_SIZE); i++) {
      uptr r = alloc_pageframe();
      ASSERT_EQ(r, RESERVED_MB * MB + i * PAGE_SIZE);
   }

   // Now we should be out-of-memory.
   ASSERT_EQ(alloc_pageframe(), 0);
}

TEST(alloc_pageframe, seq_alloc_free)
{
   init_pageframe_allocator();

   for (uptr i = 0; i < avail_pages; i++) {
      uptr r = alloc_pageframe();
      ASSERT_EQ(r, RESERVED_MB * MB + i * PAGE_SIZE);
   }

   // Now we should be out-of-memory.
   ASSERT_EQ(alloc_pageframe(), 0);

   // Free everything.
   for (uptr i = 0; i < avail_pages; i++) {
      free_pageframe(RESERVED_MB * MB + i * PAGE_SIZE);
   }

   /*
    * Now the whole memory should be free, but we cannot anticipate
    * which pageframe will be returned. Therefore, check that we can actually
    * allocate 'avail_pages' again now.
    */

   uptr allocated = 0;

   while (true) {

      if (allocated > avail_pages)
         FAIL();

      uptr paddr = alloc_pageframe();

      if (!paddr)
         break; // out-of-memory, expected.

      allocated++;
   }

   ASSERT_EQ(allocated, avail_pages);
}

TEST(alloc_pageframe, one_pageframe_free)
{
   init_pageframe_allocator();

   for (uptr i = 0; i < avail_pages; i++) {
      uptr r = alloc_pageframe();
      ASSERT_EQ(r, RESERVED_MB * MB + i * PAGE_SIZE);
   }

   // Now we should be out-of-memory.
   ASSERT_EQ(alloc_pageframe(), 0);

   // Free an arbtrary pageframe
   uptr paddr = MB * MAX_MEM_SIZE_IN_MB / 2;
   free_pageframe(paddr);

   // Now re-alloc and expect that very pageframe is returned
   ASSERT_EQ(alloc_pageframe(), paddr);
}

TEST(alloc_pageframe, perf)
{
   initialize_kmalloc_for_tests();
   kernel_alloc_pageframe_perftest();
}
