
#include <commonDefs.h>
#include <kmalloc.h>
#include <stringUtil.h>
#include <paging.h>

#define RANDOM_VALUES_COUNT 1000

extern int random_values[RANDOM_VALUES_COUNT];

void check_equal_data(int *a, int *b, size_t s)
{
   for (size_t i = 0; i < s; i++) {
      if (a[i] != b[i]) {
         printk("a[%u] != b[%u]\n", i, i);
         printk("a[%u] = %i\n", i, a[i]);
         printk("b[%u] = %i\n", i, b[i]);

         printk("&a[i] = %p\n", &a[i]);
         printk("&b[i] = %p\n", &b[i]);

         ASSERT(0);
      }
   }
}

void kmalloc_perf_test()
{
   const int iters = 1000;
   int memAllocated = 0;

   //int valuesCopy[RANDOM_VALUES_COUNT];

   void *allocations[RANDOM_VALUES_COUNT];

   printk("*** kmalloc_perf_test, %d iterations ***\n", iters);

   //memcpy(valuesCopy, random_values, sizeof(int) * RANDOM_VALUES_COUNT);

   //printk("random_values = %p\n", random_values);
   //printk("random_values[0] = %i\n", random_values[0]);

   uint64_t start = RDTSC();

   for (int i = 0; i < iters; i++) {

      for (int j = 0; j < RANDOM_VALUES_COUNT; j++) {
         allocations[j] = kmalloc(random_values[j]);

         //check_equal_data(valuesCopy, random_values, RANDOM_VALUES_COUNT);

         if (!allocations[j]) {

            printk("We were unable to allocate %u bytes\n", random_values[j]);

         } else {

            memAllocated += roundup_next_power_of_2(MAX(random_values[j], MIN_BLOCK_SIZE));
         }
      }

      for (int j = 0; j < RANDOM_VALUES_COUNT; j++) {

         //check_equal_data(valuesCopy, random_values, RANDOM_VALUES_COUNT);

         kfree(allocations[j], random_values[j]);
         memAllocated -= roundup_next_power_of_2(MAX(random_values[j], MIN_BLOCK_SIZE));

      }
   }

   uint64_t duration = (RDTSC() - start) / (iters * RANDOM_VALUES_COUNT);

   printk("Cycles per kmalloc + kfree: %llu\n", duration);
}

void kmalloc_trivial_perf_test()
{
   const int iters = 1000000;

   printk("Running a kmalloc() perf. test for %u iterations...\n", iters);

   void *b1,*b2,*b3,*b4;
   uint64_t start = RDTSC();

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

   uint64_t duration = (RDTSC() - start) / iters;

   printk("Cycles per kmalloc + kfree: %llu\n",  duration >> 2);

   ASSERT((uintptr_t)b1 == HEAP_DATA_ADDR + 0x0);
   ASSERT((uintptr_t)b2 == HEAP_DATA_ADDR + 0x20);
   ASSERT((uintptr_t)b3 == HEAP_DATA_ADDR + 0x40);
   ASSERT((uintptr_t)b4 == HEAP_DATA_ADDR + 0x0);
}
