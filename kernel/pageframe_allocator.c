
#include <common_defs.h>
#include <paging.h>
#include <string_util.h>
#include <utils.h>

#define RESERVED_MB (INITIAL_MB_RESERVED + MB_RESERVED_FOR_PAGING)
#define RESERVED_ELEMS (RESERVED_MB * 8)

#define USABLE_MEM_SIZE_IN_MB (MAX_MEM_SIZE_IN_MB - RESERVED_MB)

/*
 * By mapping 4096 KB (one page) in 1 bit, a single 32-bit integer maps 128 KB.
 * Mapping 1 MB requires 8 integers.
 */
#define FULL_128KB_AREA (0xFFFFFFFFU)


#define PAGEFRAMES_BITFIELD_ELEMS (8 * USABLE_MEM_SIZE_IN_MB)

/*
 * This bitfield maps 1 bit to 4 KB of the whole physical memory.
 * The mapping has no shifts: its bit[0] corresponds to the first KB of the
 * physical memory, as well as its N-th bit corresponds to the N-th pageframe.
 */

u32 pageframes_bitfield[8 * MAX_MEM_SIZE_IN_MB];
u32 last_index = 0;
int pageframes_used = 0;

int get_free_pageframes_count(void)
{
   return ((USABLE_MEM_SIZE_IN_MB << 20) / PAGE_SIZE) - pageframes_used;
}


void init_pageframe_allocator(void)
{

#ifdef KERNEL_TEST
   bzero((void *)pageframes_bitfield, sizeof(pageframes_bitfield));
#else
   /*
    * In the kernel, pageframes_bitfield is zeroed because it is in the BSS
    * and this function is called only ONCE. No point in clearing the bitfield.
    */
#endif

   for (int i = 0; i < RESERVED_ELEMS; i++) {
      pageframes_bitfield[i] = FULL_128KB_AREA;
   }

   init_paging_pageframe_allocator();
}

/*
 * -----------------------------------------------------
 *
 * Generic page frame allocator
 *
 * ------------------------------------------------------
 */

uptr alloc_pageframe(void)
{

   u32 free_index;
   bool found = false;

   /*
    * Optimization: use a shifted bitfield in order to skip the initial
    * reserved elems. For example, with the first 4 MB reserved, we're talking
    * about 32 elems. It's not that much, but still: why doing that extra work?
    */
   u32 * const bitfield = pageframes_bitfield + RESERVED_ELEMS;

   for (int i = 0; i < PAGEFRAMES_BITFIELD_ELEMS; i++) {

      if (bitfield[last_index] != FULL_128KB_AREA) {
         found = true;
         break;
      }

      last_index = (last_index + 1) % PAGEFRAMES_BITFIELD_ELEMS;
   }

   if (!found) {
      return 0;
   }

   free_index = get_first_zero_bit_index(bitfield[last_index]);
   bitfield[last_index] |= (1 << free_index);

   pageframes_used++;

   /*
    * Because we used a shift-ed bitfield, we have to calculate the index
    * for the real bitfield.
    */
   const u32 actual_index = last_index + RESERVED_ELEMS;
   return ((actual_index << 17) + (free_index << PAGE_SHIFT));
}


void free_pageframe(uptr address) {

   uptr naddr = address & PAGE_MASK;
   u32 bitIndex = (naddr >> PAGE_SHIFT) & 31;
   u32 majorIndex = (naddr & 0xFFFE0000U) >> 17;

   // Asserts that the page was allocated.
   ASSERT(pageframes_bitfield[majorIndex] & (1 << bitIndex));

   pageframes_bitfield[majorIndex] &= ~(1 << bitIndex);

   /*
    * Make the last index to point here, where we have at least 1 page free.
    * This increases the data locality.
    */
   last_index = (majorIndex - RESERVED_ELEMS) % PAGEFRAMES_BITFIELD_ELEMS;

   pageframes_used--;
}


#ifdef DEBUG

bool is_allocated_pageframe(uptr address)
{
   uptr naddr = address & PAGE_MASK;
   u32 bitIndex = (naddr >> PAGE_SHIFT) & 31;
   u32 majorIndex = (naddr & 0xFFFE0000U) >> 17;
   return !!(pageframes_bitfield[majorIndex] & (1 << bitIndex));
}

#endif
