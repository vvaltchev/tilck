
#include <commonDefs.h>
#include <kmalloc.h>
#include <stringUtil.h>
#include <paging.h>
#include <utils.h>
#include <arch/generic_x86/utils.h>


#define RANDOM_VALUES_COUNT 1000

extern int random_values[RANDOM_VALUES_COUNT];

void kmalloc_perf_test()
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

         } else {

            memAllocated += roundup_next_power_of_2(MAX(random_values[j], MIN_BLOCK_SIZE));
         }
      }

      for (int j = 0; j < RANDOM_VALUES_COUNT; j++) {
         kfree(allocations[j], random_values[j]);
         memAllocated -= roundup_next_power_of_2(MAX(random_values[j], MIN_BLOCK_SIZE));
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
