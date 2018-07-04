
#include <gtest/gtest.h>
#include "kernel_init_funcs.h"

extern "C" {
   #include <exos/common/basic_defs.h>
   #include <exos/kernel/paging.h>
   #include <exos/kernel/pageframe_allocator.h>
   #include <exos/kernel/self_tests/self_tests.h>
   extern u32 memsize_in_mb;
}

TEST(alloc_pageframe, seq_alloc)
{
   init_pageframe_allocator();

   ASSERT_EQ((uptr)get_usable_pg_count(), 128 * MB / PAGE_SIZE);

   for (uptr i = 0; ; i++) {

      uptr r = alloc_pageframe();

      if (r == INVALID_PADDR)
         break;

      ASSERT_EQ(r, LINEAR_MAPPING_SIZE + i * PAGE_SIZE);
   }

   // Now we should be out-of-memory.
   ASSERT_EQ(get_free_pg_count(), 0);

   ASSERT_EQ((uptr)get_used_pg_count(),
             (get_phys_mem_mb() - LINEAR_MAPPING_MB) * MB / PAGE_SIZE);
}

TEST(alloc_pageframe, seq_alloc_free)
{
   init_pageframe_allocator();

   for (uptr i = 0; ; i++) {

      uptr r = alloc_pageframe();

      if (r == INVALID_PADDR)
         break;

      ASSERT_EQ(r, LINEAR_MAPPING_SIZE + i * PAGE_SIZE);
   }

   // Now we should be out-of-memory.
   ASSERT_EQ(get_free_pg_count(), 0);

   // Free everything.
   for (int i = 0; i < get_usable_pg_count(); i++) {
      free_pageframe(LINEAR_MAPPING_SIZE + i * PAGE_SIZE);
   }

   ASSERT_EQ(get_free_pg_count(), get_usable_pg_count());

   /*
    * Now the whole memory should be free, but we cannot anticipate
    * which pageframe will be returned. Therefore, check that we can actually
    * allocate 'avail_pages' again now.
    */

   int allocated = 0;

   while (true) {

      if (allocated > get_usable_pg_count())
         FAIL();

      uptr r = alloc_pageframe();

      if (r == INVALID_PADDR)
         break;

      allocated++;
   }

   ASSERT_EQ(allocated, get_usable_pg_count());
   ASSERT_EQ(get_free_pg_count(), 0);
}

TEST(alloc_pageframe, one_pageframe_free)
{
   init_pageframe_allocator();

   for (uptr i = 0; ; i++) {

      uptr r = alloc_pageframe();

      if (r == INVALID_PADDR)
         break;

      ASSERT_EQ(r, LINEAR_MAPPING_SIZE + i * PAGE_SIZE);
   }

   // Now we should be out-of-memory.
   ASSERT_EQ(get_free_pg_count(), 0);

   // Free an arbtrary pageframe
   uptr paddr = LINEAR_MAPPING_SIZE + 5 * MB;
   free_pageframe(paddr);

   // Now re-alloc and expect that very pageframe is returned
   ASSERT_EQ(alloc_pageframe(), paddr);
}

TEST(alloc_pageframe, hybrid)
{
   init_pageframe_allocator();

   uptr paddr = alloc_32_pageframes();
   ASSERT_TRUE(paddr != INVALID_PADDR);

   free_8_pageframes(paddr + PAGE_SIZE * 0);
   free_8_pageframes(paddr + PAGE_SIZE * 8);
   free_8_pageframes(paddr + PAGE_SIZE * 16);
   free_8_pageframes(paddr + PAGE_SIZE * 24);
}

TEST(alloc_pageframe, perf)
{
   init_kmalloc_for_tests();
   selftest_alloc_pageframe_perf();
}
