
#include <commonDefs.h>
#include <kmalloc.h>
#include <stringUtil.h>
#include <paging.h>

void kmalloc_trivial_perf_test()
{
   const int iters = 1000000;

   printk("Running a kmalloc() perf. test for %u iterations...\n", iters);

   void *b1,*b2,*b3,*b4;
   uintptr_t start = RDTSC();

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

   uintptr_t duration = (RDTSC() - start) / iters;

   printk("Cycles per kmalloc + kfree: %u\n",  duration >> 2);

   ASSERT((uintptr_t)b1 == HEAP_DATA_ADDR + 0x0);
   ASSERT((uintptr_t)b2 == HEAP_DATA_ADDR + 0x20);
   ASSERT((uintptr_t)b3 == HEAP_DATA_ADDR + 0x40);
   ASSERT((uintptr_t)b4 == HEAP_DATA_ADDR + 0x0);
}
