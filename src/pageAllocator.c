
#include <paging.h>
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
volatile int pagesUsed = 0;

int get_free_physical_pages_count()
{
   return (MEM_SIZE_IN_MB / PAGE_SIZE) - pagesUsed;
}

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
      pagesUsed += 32;
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

   pagesUsed++;
   return (void *)ret;
}


void free_phys_page(void *address) {

   uintptr_t naddr = ((uintptr_t)address) & 0xFFFFF000U;
   uint32_t bitIndex = (naddr >> 12) & 31;
   uint32_t majorIndex = (naddr & 0xFFFE0000U) >> 17;

   // Asserts that the page was allocated.
   ASSERT(pages_bit_field[majorIndex] & (1 << bitIndex));

   pages_bit_field[majorIndex] &= ~(1 << bitIndex);

   /*
    * Make the last index to point here, where we have at least 1 page free.
    * This increases the data locality.
    */
   last_index = majorIndex;

   pagesUsed--;
}


bool kbasic_virtual_alloc(uintptr_t vaddr, size_t size)
{
   ASSERT(size > 0);        // the size must be > 0.
   ASSERT(!(size & 4095));  // the size must be a multiple of 4096
   ASSERT(!(vaddr & 4095)); // the vaddr must be page-aligned

   page_directory_t *pdir = get_curr_page_dir();

   int pagesCount = (int) (size >> 12);

   if (get_free_physical_pages_count() < pagesCount)
      return false;

   for (int i = 0; i < pagesCount; i++)
      if (is_mapped(pdir, vaddr + (i << 12)))
         return false;

   for (int i = 0; i < pagesCount; i++) {

      void *paddr = alloc_phys_page();
      ASSERT(paddr != NULL);

      map_page(pdir, vaddr + (i << 12), (uintptr_t)paddr, false, true);
   }

   return true;
}

bool kbasic_virtual_free(uintptr_t vaddr, uintptr_t size)
{
   ASSERT(size > 0);        // the size must be > 0.
   ASSERT(!(size & 4095));  // the size must be a multiple of 4096
   ASSERT(!(vaddr & 4095)); // the vaddr must be page-aligned

   page_directory_t *pdir = get_curr_page_dir();

   unsigned pagesCount = size >> 12;

   for (unsigned i = 0; i < pagesCount; i++)
      if (!is_mapped(pdir, vaddr + (i << 12)))
         return false;

   for (unsigned i = 0; i < pagesCount; i++) {

      uintptr_t va = vaddr + (i << 12);

      // un-map the virtual address.
      unmap_page(pdir, va);

      // free the physical page as well.
      free_phys_page((void *) KERNEL_VADDR_TO_PADDR(va));
   }

   return true;
}