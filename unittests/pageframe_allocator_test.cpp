
#include <gtest/gtest.h>
#include "kernel_init_funcs.h"

extern "C" {
   #include <common_defs.h>
   #include <paging.h>
   #include <pageframe_allocator.h>
   #include <self_tests/self_tests.h>
   uptr paging_alloc_pageframe();
   void paging_free_pageframe(uptr address);
}

TEST(alloc_pageframe, seq_alloc)
{
   init_pageframe_allocator();

   for (uptr i = 0; ; i++) {

      uptr r = alloc_pageframe();

      if (r == INVALID_PADDR)
         break;

      ASSERT_EQ(r, i * PAGE_SIZE);
   }

   // Now we should be out-of-memory.
   ASSERT_EQ(get_free_pageframes_count(), 0);

   ASSERT_EQ(get_used_pageframes_count(),
             get_phys_mem_mb() * MB / PAGE_SIZE);
}

TEST(alloc_pageframe, seq_alloc_free)
{
   init_pageframe_allocator();

   for (uptr i = 0; ; i++) {

      uptr r = alloc_pageframe();

      if (r == INVALID_PADDR)
         break;

      ASSERT_EQ(r, i * PAGE_SIZE);
   }

   // Now we should be out-of-memory.
   ASSERT_EQ(get_free_pageframes_count(), 0);

   // Free everything.
   for (uptr i = 0; i < get_total_pageframes_count(); i++) {
      free_pageframe(i * PAGE_SIZE);
   }

   ASSERT_EQ(get_free_pageframes_count(), get_total_pageframes_count());

   /*
    * Now the whole memory should be free, but we cannot anticipate
    * which pageframe will be returned. Therefore, check that we can actually
    * allocate 'avail_pages' again now.
    */

   uptr allocated = 0;

   while (true) {

      if (allocated > get_total_pageframes_count())
         FAIL();

      uptr r = alloc_pageframe();

      if (r == INVALID_PADDR)
         break;

      allocated++;
   }

   ASSERT_EQ(allocated, get_total_pageframes_count());
   ASSERT_EQ(get_free_pageframes_count(), 0);
}

TEST(alloc_pageframe, one_pageframe_free)
{
   init_pageframe_allocator();

   for (uptr i = 0; ; i++) {

      uptr r = alloc_pageframe();

      if (r == INVALID_PADDR)
         break;

      ASSERT_EQ(r, i * PAGE_SIZE);
   }

   // Now we should be out-of-memory.
   ASSERT_EQ(get_free_pageframes_count(), 0);

   // Free an arbtrary pageframe
   uptr paddr = MB * get_phys_mem_mb() / 2;
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
   initialize_kmalloc_for_tests();
   kernel_alloc_pageframe_perftest();
}
