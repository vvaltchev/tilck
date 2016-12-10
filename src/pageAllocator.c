
#include <commonDefs.h>
#include <paging.h>
#include <stringUtil.h>

#define MEM_SIZE_IN_MB 128
#define INITIAL_MB_RESERVED 2
#define MB_RESERVED_FOR_PAGING 2

#define USABLE_MEM_SIZE_IN_MB (MEM_SIZE_IN_MB - INITIAL_MB_RESERVED - MB_RESERVED_FOR_PAGING)

/*
 * By mapping 4096 KB (one page) in 1 bit, a single 32-bit integer maps 128 KB.
 * Mapping 1 MB requires 8 integers.
 */
#define FULL_128KB_AREA (0xFFFFFFFFU)


#define INITIAL_ELEMS_RESERVED (INITIAL_MB_RESERVED * 8)
#define ELEMS_RESERVED_FOR_PAGING (MB_RESERVED_FOR_PAGING * 8)

#define PAGES_BIT_FIELD_ELEMS (8 * USABLE_MEM_SIZE_IN_MB)


volatile u32 pages_bit_field[8 * MEM_SIZE_IN_MB] = {0};
volatile u32 last_index = 0;
volatile int pagesUsed = 0;

int get_free_physical_pages_count()
{
   return ((USABLE_MEM_SIZE_IN_MB << 20) / PAGE_SIZE) - pagesUsed;
}

static u32 get_first_zero_bit_index(u32 num)
{
   u32 i;

   for (i = 0; i < 32; i++) {
      if ((num & (1U << i)) == 0) break;
   }

   return i;
}

void init_physical_page_allocator()
{
   //// Mark the first 2 MBs as used.
   //for (u32 i = 0; i < INITIAL_ELEMS_RESERVED; i++) {
   //   pages_bit_field[i] = FULL_128KB_AREA;
   //   pagesUsed += 32;
   //}
}

/*
 * Paging needs its custom physical page allocator for page tables.
 */

void *paging_alloc_phys_page()
{
   u32 index = 0;
   bool found = false;

   volatile u32 * const bitfield =
      pages_bit_field + INITIAL_ELEMS_RESERVED;

   for (int i = 0; i < ELEMS_RESERVED_FOR_PAGING; i++) {

      if (bitfield[index] != FULL_128KB_AREA) {
         found = true;
         break;
      }

      index = (index + 1) % ELEMS_RESERVED_FOR_PAGING;
   }

   ASSERT(found);

   uptr ret;

   u32 free_index = get_first_zero_bit_index(bitfield[index]);  
   bitfield[index] |= (1 << free_index);

   ret = (( (index + INITIAL_ELEMS_RESERVED) << 17) + (free_index << PAGE_SHIFT));
   return (void *)ret;
}


void paging_free_phys_page(void *address) {

   uptr naddr = ((uptr)address) & 0xFFFFF000U;
   u32 bitIndex = (naddr >> PAGE_SHIFT) & 31;
   u32 majorIndex = (naddr & 0xFFFE0000U) >> 17;

   // Asserts that the page was allocated.
   ASSERT(pages_bit_field[majorIndex] & (1 << bitIndex));

   pages_bit_field[majorIndex] &= ~(1 << bitIndex);
}


/*
 * -----------------------------------------------------
 *
 * Generic physical page allocator
 *
 * ------------------------------------------------------
 */

void *alloc_phys_page()
{

   u32 free_index;
   bool found = false;

   volatile u32 * const bitfield =
      pages_bit_field + INITIAL_ELEMS_RESERVED + ELEMS_RESERVED_FOR_PAGING;

   for (int i = 0; i < PAGES_BIT_FIELD_ELEMS; i++) {

      if (bitfield[last_index] != FULL_128KB_AREA) {
         found = true;
         break;
      }

      last_index = (last_index + 1) % PAGES_BIT_FIELD_ELEMS;
   }

   if (!found) {
      return NULL;
   }

   uptr ret;

   free_index = get_first_zero_bit_index(bitfield[last_index]);
   bitfield[last_index] |= (1 << free_index);

   pagesUsed++;

   const u32 actual_index =
      last_index + INITIAL_ELEMS_RESERVED + ELEMS_RESERVED_FOR_PAGING;

   ret = ((actual_index << 17) + (free_index << PAGE_SHIFT));
   return (void *)ret;
}


void free_phys_page(void *address) {

   uptr naddr = ((uptr)address) & 0xFFFFF000U;
   u32 bitIndex = (naddr >> PAGE_SHIFT) & 31;
   u32 majorIndex = (naddr & 0xFFFE0000U) >> 17;

   // Asserts that the page was allocated.
   ASSERT(pages_bit_field[majorIndex] & (1 << bitIndex));

   pages_bit_field[majorIndex] &= ~(1 << bitIndex);

   /*
    * Make the last index to point here, where we have at least 1 page free.
    * This increases the data locality.
    */
   last_index = (majorIndex - INITIAL_ELEMS_RESERVED - ELEMS_RESERVED_FOR_PAGING) % PAGES_BIT_FIELD_ELEMS;

   pagesUsed--;
}
