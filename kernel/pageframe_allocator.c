
#include <common_defs.h>
#include <paging.h>
#include <string_util.h>
#include <utils.h>

#define RESERVED_MB (INITIAL_MB_RESERVED + MB_RESERVED_FOR_PAGING)
#define RESERVED_ELEMS (RESERVED_MB * 8)

/*
 * By mapping 4096 KB (one page) in 1 bit, a single 32-bit integer maps 128 KB.
 * Mapping 1 MB requires 8 integers.
 */
#define FULL_128KB_AREA (0xFFFFFFFFU)



/*
 * This bitfield maps 1 bit to 4 KB of the whole physical memory.
 * The mapping has no shifts: its bit[0] corresponds to the first KB of the
 * physical memory, as well as its N-th bit corresponds to the N-th pageframe.
 */

u32 pageframes_bitfield[8 * MAX_MEM_SIZE_IN_MB];

/*
 * HACK: just assume we have MAX_MEM_SIZE_IN_MB as memory.
 * In the future, this value has to be somehow passed from the bootloader to
 * the kernel.
 */
u32 memsize_in_mb = MAX_MEM_SIZE_IN_MB;

u32 last_index;
int pageframes_used;

int get_total_pageframes_count(void)
{
   return (memsize_in_mb << 20) >> PAGE_SHIFT;
}

int get_free_pageframes_count(void)
{
   return get_total_pageframes_count() - pageframes_used;
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

   last_index = 0;
   pageframes_used = (RESERVED_MB * MB) / PAGE_SIZE;

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

   for (u32 i = 0; i < ARRAY_SIZE(pageframes_bitfield); i++) {

      if (pageframes_bitfield[last_index] != FULL_128KB_AREA) {
         found = true;
         break;
      }

      last_index = (last_index + 1) % ARRAY_SIZE(pageframes_bitfield);
   }

   if (!found) {
      return 0;
   }

   free_index = get_first_zero_bit_index(pageframes_bitfield[last_index]);
   pageframes_bitfield[last_index] |= (1 << free_index);

   pageframes_used++;
   return ((last_index << 17) + (free_index << PAGE_SHIFT));
}

uptr alloc_32_pageframes_aligned(void)
{
   bool found = false;

   for (u32 i = 0; i < ARRAY_SIZE(pageframes_bitfield); i++) {

      if (pageframes_bitfield[last_index] == 0) {
         found = true;
         break;
      }

      last_index = (last_index + 1) % ARRAY_SIZE(pageframes_bitfield);
   }

   if (!found)
      return 0;

   pageframes_bitfield[last_index] = FULL_128KB_AREA;
   pageframes_used += 32;
   return last_index << 17;
}

uptr alloc_32_pageframes(void)
{
   // uptr paddr = alloc_32_pageframes_aligned();

   // if (paddr)
   //    return paddr;

   u32 li = last_index << 2;

   for (u32 i = 0; i < sizeof(pageframes_bitfield); i++) {

      if ((li + 4) >= sizeof(pageframes_bitfield)) {
         li = 0;
      }

      u32 *bf = (u32 *)((u8 *)pageframes_bitfield + li);

      if (*bf == 0) {

         *bf = FULL_128KB_AREA;
         pageframes_used += 32;

         /*
          * In this bitfield 1 bit = 1 page, 1 byte = 8 pages (2^3 pages).
          * Therefore the corresponding physical address is just:
          * i * PAGE_SIZE * 8.
          */
         last_index = (li >> 2);
         return li << (PAGE_SHIFT + 3);
      }

      li++;
   }

   last_index = (li >> 2);
   return 0;
}

void free_32_pageframes(uptr paddr)
{
   u32 byte_offset = paddr >> (PAGE_SHIFT + 3);
   ASSERT(*(u32 *)((u8 *)pageframes_bitfield + byte_offset) == FULL_128KB_AREA);
   *(u32 *)((u8 *)pageframes_bitfield + byte_offset) = 0;
   pageframes_used -= 32;
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
   last_index = majorIndex % ARRAY_SIZE(pageframes_bitfield);

   pageframes_used--;
}


bool is_allocated_pageframe(uptr address)
{
   uptr naddr = address & PAGE_MASK;
   u32 bitIndex = (naddr >> PAGE_SHIFT) & 31;
   u32 majorIndex = (naddr & 0xFFFE0000U) >> 17;
   return !!(pageframes_bitfield[majorIndex] & (1 << bitIndex));
}
