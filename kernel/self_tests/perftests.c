
#include <common_defs.h>
#include <kmalloc.h>
#include <string_util.h>
#include <paging.h>
#include <utils.h>
#include <arch/generic_x86/x86_utils.h>


#define RANDOM_VALUES_COUNT 1000

extern int random_values[RANDOM_VALUES_COUNT];

void kernel_kmalloc_perf_test()
{
   const int iters = 1000;
   int memAllocated = 0;

   void *allocations[RANDOM_VALUES_COUNT];

   printk("*** kmalloc_perf_test, %d iterations ***\n", iters);

   u64 start = RDTSC();

   for (int i = 0; i < iters; i++) {

      for (int j = 0; j < RANDOM_VALUES_COUNT; j++) {
         allocations[j] = kmalloc(random_values[j]);

         if (!allocations[j]) {

            printk("We were unable to allocate %u bytes\n", random_values[j]);

#ifdef KERNEL_TEST
            NOT_REACHED();
#endif

         } else {

            memAllocated +=
               roundup_next_power_of_2(MAX(random_values[j], MIN_BLOCK_SIZE));
         }
      }

      for (int j = 0; j < RANDOM_VALUES_COUNT; j++) {
         kfree(allocations[j], random_values[j]);

         memAllocated -=
            roundup_next_power_of_2(MAX(random_values[j], MIN_BLOCK_SIZE));
      }
   }

   u64 duration = (RDTSC() - start) / (iters * RANDOM_VALUES_COUNT);

   printk("Cycles per kmalloc + kfree: %llu\n", duration);
}

void kmalloc_trivial_perf_test()
{
   const int iters = 1000000;

   printk("Trivial kmalloc() perf. test for %u iterations...\n", iters);

   void *b1,*b2,*b3,*b4;
   u64 start = RDTSC();

   for (int i = 0; i < iters; i++) {

      b1 = kmalloc(10);
      b2 = kmalloc(10);
      b3 = kmalloc(50);

      kfree(b1, 10);
      kfree(b2, 10);
      kfree(b3, 50);

      b4 = kmalloc(3 * PAGE_SIZE + 43);
      kfree(b4, 3 * PAGE_SIZE + 43);
   }

   u64 duration = (RDTSC() - start) / iters;

   printk("Cycles per kmalloc + kfree: %llu\n",  duration >> 2);

   ASSERT((uptr)b1 == HEAP_DATA_ADDR + 0x0);
   ASSERT((uptr)b2 == HEAP_DATA_ADDR + 0x20);
   ASSERT((uptr)b3 == HEAP_DATA_ADDR + 0x40);
   ASSERT((uptr)b4 == HEAP_DATA_ADDR + 0x0);
}

static int calc_perc_free_pageframes(void)
{
   return 100*get_free_pageframes_count()/get_total_pageframes_count();
}

static void print_free_pageframes(void)
{
   printk("Free pageframes: %u/%u [%u%%]\n",
          get_free_pageframes_count(),
          get_total_pageframes_count(),
          calc_perc_free_pageframes());
}

void kernel_alloc_pageframe_perftest_perc_free(const int free_perc_threshold)
{
   u32 allocated = 0;
   const u32 max_pages = MAX_MEM_SIZE_IN_MB * MB / PAGE_SIZE;
   uptr *paddrs = kmalloc(max_pages * sizeof(uptr));

   u64 start, duration;

   // Prepare a random bitfield for tests

   allocated = 0;
   const int expected_free = RANDOM_VALUES_COUNT/2; // Don't change.

   // Alloc pageframes until only 'expected_free' 32-bit integers remain.
   for (int i = 0; i < expected_free; i++) {
      for (int j = 0; j < 32; j++) {
         uptr paddr = alloc_pageframe();
         VERIFY(paddr != 0);
         paddrs[allocated++] = paddr;
      }
   }

   int iters = 0;

   for (int i = 0;
        calc_perc_free_pageframes() > free_perc_threshold;
        i = (i + 2) % RANDOM_VALUES_COUNT) {

      iters++;

      u32 shift = random_values[i] % 31;
      u32 val = random_values[i] | (random_values[i+1] << shift);
      u32 f = 0;

      for (u32 j = 0; j < 32; j++) {
         if ((val & (1U << j)) == 0)
            f++;
      }

      if (f > 16) {
         f = 32 - f;
         val = ~val;
      }


      /*
       * Now, ideally we'd like just to set an integer in the pageframe
       * bitfield to have the value 'val'. But, in order to support any further
       * implementation change in the pageframe allocator, we won't touch its
       * data. We'll hack it using its external interface.
       */

      uptr local_paddrs[32];

      // 1. Alloc 32 pageframes.
      for (int j = 0; j < 32; j++) {
         local_paddrs[j] = alloc_pageframe();
         VERIFY(local_paddrs[j] != 0);
      }

      // 2. Free 'f' of them, according to the 0s in 'val'.
      for (int j = 0; j < 32; j++) {
         if ((val & (1U << j)) == 0) {
            free_pageframe(local_paddrs[j]);
            local_paddrs[j] = 0;
         }
      }

      // 3. Save the allocated pageframes in 'paddrs' for the clean-up.
      for (int j = 0; j < 32; j++) {
         if (local_paddrs[j])
            paddrs[allocated++] = local_paddrs[j];
      }

   }

   /*
    * How many iterations did we before reaching the free_perc_threshold ?
    * If we did too many iterations (much more than RANDOM_VALUES_COUNT/2)
    * that means that we reused too many times the random values, therefore
    * there are repeating patterns and this is bad for the quality of the test.
    * The check below ensures that we reused not more than 25% of the random
    * values twice.
    */
   VERIFY(100 * iters / (RANDOM_VALUES_COUNT/2) <= 125);

   const int free_pageframes_count = get_free_pageframes_count();

   start = RDTSC();
   for (int i = 0; i < free_pageframes_count; i++) {
      uptr paddr = alloc_pageframe();
      VERIFY(paddr != 0);
      paddrs[allocated++] = paddr;
   }
   duration = RDTSC() - start;

   printk("AVG cost of alloc with %i%% of free pageframes: %i cycles\n",
          free_perc_threshold, duration/free_pageframes_count);

   for (u32 i = 0; i < allocated; i++) {
      free_pageframe(paddrs[i]);
   }

   kfree(paddrs, max_pages * sizeof(uptr));
}

void kernel_alloc_pageframe_perftest(void)
{
   u32 allocated = 0;
   const u32 max_pages = MAX_MEM_SIZE_IN_MB * MB / PAGE_SIZE;
   uptr *paddrs = kmalloc(max_pages * sizeof(uptr));

   u64 start, duration;

   start = RDTSC();
   while (true) {

      uptr paddr = alloc_pageframe();

      if (!paddr)
         break;

      paddrs[allocated++] = paddr;
   }
   duration = RDTSC() - start;

   u32 avg = duration / allocated;
   printk("Allocated %u pageframes, AVG cost: %u cycles\n", allocated, avg);

   // Now let's free just one pageframe in somewhere in the middle

   uptr target = paddrs[allocated/2];

   // And re-allocate it.
   const int single_page_iters = 10000;

   start = RDTSC();
   for (int i = 0; i < single_page_iters; i++) {
      free_pageframe(target);
      alloc_pageframe();      // returns always 'target'
   }
   duration = RDTSC() - start;

   u32 one_page_free_avg = duration/single_page_iters;
   printk("[1-page free] alloc + free: %u cycles\n", one_page_free_avg);

   start = RDTSC();
   for (u32 i = 0; i < allocated; i++) {
      free_pageframe(paddrs[i]);
   }
   duration = RDTSC() - start;

   avg = duration / allocated;
   printk("Freed %u pageframes, AVG cost: %u cycles\n", allocated, avg);

   kfree(paddrs, max_pages * sizeof(uptr));

   kernel_alloc_pageframe_perftest_perc_free(1);
   kernel_alloc_pageframe_perftest_perc_free(2);
   kernel_alloc_pageframe_perftest_perc_free(5);
   kernel_alloc_pageframe_perftest_perc_free(10);
   kernel_alloc_pageframe_perftest_perc_free(20);
   kernel_alloc_pageframe_perftest_perc_free(40);
}
