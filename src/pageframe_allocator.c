
#include <commonDefs.h>
#include <paging.h>
#include <string_util.h>

#define MEM_SIZE_IN_MB 128
#define INITIAL_MB_RESERVED 2
#define MB_RESERVED_FOR_PAGING 2

#define USABLE_MEM_SIZE_IN_MB \
   (MEM_SIZE_IN_MB - INITIAL_MB_RESERVED - MB_RESERVED_FOR_PAGING)

/*
 * By mapping 4096 KB (one page) in 1 bit, a single 32-bit integer maps 128 KB.
 * Mapping 1 MB requires 8 integers.
 */
#define FULL_128KB_AREA (0xFFFFFFFFU)


#define INITIAL_ELEMS_RESERVED (INITIAL_MB_RESERVED * 8)
#define ELEMS_RESERVED_FOR_PAGING (MB_RESERVED_FOR_PAGING * 8)

#define PAGEFRAMES_BITFIELD_ELEMS (8 * USABLE_MEM_SIZE_IN_MB)


volatile u32 pageframes_bitfield[8 * MEM_SIZE_IN_MB] = {0};
volatile u32 last_index = 0;
volatile int pageframes_used = 0;

int get_free_pageframes_count()
{
   return ((USABLE_MEM_SIZE_IN_MB << 20) / PAGE_SIZE) - pageframes_used;
}

static u32 get_first_zero_bit_index(u32 num)
{
   u32 i;

   for (i = 0; i < 32; i++) {
      if ((num & (1U << i)) == 0) break;
   }

   return i;
}

void init_pageframe_allocator()
{
   /* Do nothing (for the moment). */
}

/*
 * Paging needs its custom page frame allocator for page tables.
 */

void *paging_alloc_pageframe()
{
   u32 index = 0;
   bool found = false;

   volatile u32 * const bitfield =
      pageframes_bitfield + INITIAL_ELEMS_RESERVED;

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


void paging_free_pageframe(void *address) {

   uptr naddr = ((uptr)address) & 0xFFFFF000U;
   u32 bitIndex = (naddr >> PAGE_SHIFT) & 31;
   u32 majorIndex = (naddr & 0xFFFE0000U) >> 17;

   // Asserts that the page was allocated.
   ASSERT(pageframes_bitfield[majorIndex] & (1 << bitIndex));

   pageframes_bitfield[majorIndex] &= ~(1 << bitIndex);
}


/*
 * -----------------------------------------------------
 *
 * Generic page frame allocator
 *
 * ------------------------------------------------------
 */

void *alloc_pageframe()
{

   u32 free_index;
   bool found = false;

   volatile u32 * const bitfield =
      pageframes_bitfield + INITIAL_ELEMS_RESERVED + ELEMS_RESERVED_FOR_PAGING;

   for (int i = 0; i < PAGEFRAMES_BITFIELD_ELEMS; i++) {

      if (bitfield[last_index] != FULL_128KB_AREA) {
         found = true;
         break;
      }

      last_index = (last_index + 1) % PAGEFRAMES_BITFIELD_ELEMS;
   }

   if (!found) {
      return NULL;
   }

   uptr ret;

   free_index = get_first_zero_bit_index(bitfield[last_index]);
   bitfield[last_index] |= (1 << free_index);

   pageframes_used++;

   const u32 actual_index =
      last_index + INITIAL_ELEMS_RESERVED + ELEMS_RESERVED_FOR_PAGING;

   ret = ((actual_index << 17) + (free_index << PAGE_SHIFT));
   return (void *)ret;
}


void free_pageframe(void *address) {

   uptr naddr = ((uptr)address) & 0xFFFFF000U;
   u32 bitIndex = (naddr >> PAGE_SHIFT) & 31;
   u32 majorIndex = (naddr & 0xFFFE0000U) >> 17;

   // Asserts that the page was allocated.
   ASSERT(pageframes_bitfield[majorIndex] & (1 << bitIndex));

   pageframes_bitfield[majorIndex] &= ~(1 << bitIndex);

   /*
    * Make the last index to point here, where we have at least 1 page free.
    * This increases the data locality.
    */
   last_index = (majorIndex
                 - INITIAL_ELEMS_RESERVED
                 - ELEMS_RESERVED_FOR_PAGING) % PAGEFRAMES_BITFIELD_ELEMS;

   pageframes_used--;
}
