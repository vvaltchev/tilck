
#include <gtest/gtest.h>

extern "C" {
#include <common_defs.h>
#include <paging.h>
uptr paging_alloc_pageframe();
void paging_free_pageframe(uptr address);
}


TEST(paging_alloc_pageframe, seq_alloc)
{
   init_pageframe_allocator();

   for (uptr i = 0; i < (MB * MB_RESERVED_FOR_PAGING)/PAGE_SIZE; i++) {
      uptr r = paging_alloc_pageframe();
      ASSERT_EQ(r, INITIAL_MB_RESERVED * MB + i * PAGE_SIZE);
   }

   // Now we should be out-of-memory for the paging.
   ASSERT_EQ(paging_alloc_pageframe(), INVALID_PADDR);
}


TEST(paging_alloc_pageframe, seq_alloc_free)
{
   init_pageframe_allocator();

   for (uptr i = 0; i < (MB * MB_RESERVED_FOR_PAGING)/PAGE_SIZE; i++) {
      uptr r = paging_alloc_pageframe();
      ASSERT_EQ(r, INITIAL_MB_RESERVED * MB + i * PAGE_SIZE);
   }

   // Now we should be out-of-memory for the paging.
   ASSERT_EQ(paging_alloc_pageframe(), INVALID_PADDR);

   // Free everything.
   for (uptr i = 0; i < (MB * MB_RESERVED_FOR_PAGING)/PAGE_SIZE; i++) {
      paging_free_pageframe(INITIAL_MB_RESERVED * MB + i * PAGE_SIZE);
   }

   // Now the whole paging memory should be free
   ASSERT_EQ(paging_alloc_pageframe(), INITIAL_MB_RESERVED * MB);
}

TEST(paging_alloc_pageframe, one_pageframe_free)
{
   init_pageframe_allocator();

   for (uptr i = 0; i < (MB * MB_RESERVED_FOR_PAGING)/PAGE_SIZE; i++) {
      uptr r = paging_alloc_pageframe();
      ASSERT_EQ(r, INITIAL_MB_RESERVED * MB + i * PAGE_SIZE);
   }

   // Now we should be out-of-memory for the paging.
   ASSERT_EQ(paging_alloc_pageframe(), INVALID_PADDR);

   // Free an arbtrary pageframe within the rage.
   uptr paddr = INITIAL_MB_RESERVED * MB + (MB * MB_RESERVED_FOR_PAGING)/2;
   paging_free_pageframe(paddr);

   // Now re-alloc and expect that very pageframe is returned
   ASSERT_EQ(paging_alloc_pageframe(), paddr);
}
