
#include <kmalloc.h>
#include <stringUtil.h>

#define MEM_SIZE_IN_MB 128

/*
 * By mapping 4096 KB (one page) in 1 bit, a sigle 32-bit integer maps 128 KB.
 * Mapping 1 MB requires 8 integers.
 */
#define PAGES_BIT_FIELD_ELEMS (8 * MEM_SIZE_IN_MB)

#define FULL_128KB_AREA (0xFFFFFFFFU)

volatile uint32_t pages_bit_field[PAGES_BIT_FIELD_ELEMS] = {0};
volatile uint32_t last_index = 0;

static uint32_t get_first_zero_bit_index(uint32_t num)
{
   uint32_t i;

   for (i = 0; i < 32; i++) {
      if ((num & (1U << i)) == 0) break;
   }

   return i;
}

void init_physical_page_allocator()
{
   // Mark the first 2 MBs as used.
   for (uint32_t i = 0; i < 16; i++) {
      pages_bit_field[i] = FULL_128KB_AREA;
   }
}

void *alloc_phys_page() {

   uint32_t free_index;
   bool found = false;

   for (int i = 0; i < PAGES_BIT_FIELD_ELEMS; i++) {

      if (pages_bit_field[last_index] != FULL_128KB_AREA) {
         found = true;
         break;
      }

      last_index = (last_index + 1) % PAGES_BIT_FIELD_ELEMS;
   }

   if (!found) {
      return NULL;
   }

   uintptr_t ret;

   free_index = get_first_zero_bit_index(pages_bit_field[last_index]);
   ret = ((last_index << 17) + (free_index << 12));
   pages_bit_field[last_index] |= (1 << free_index);

   //printk("[alloc_page] Returning: %p\n", ret);
   return (void *)ret;
}


void free_phys_page(void *address) {

   uintptr_t naddr = ((uintptr_t)address) & 0xFFFFF000U;
   uint32_t bitIndex = (naddr >> 12) & 31;
   uint32_t majorIndex = (naddr & 0xFFFE0000U) >> 17;

   //printk("[free_page]: addr: %p\n", naddr);

   pages_bit_field[majorIndex] &= ~(1 << bitIndex);

   /*
    * Make the last index to point here, where we have at least 1 page free.
    * This increases the data locality.
    */
   last_index = majorIndex;
}
