
#include <common_defs.h>
#include <paging.h>
#include <string_util.h>
#include <utils.h>

/*
 * By mapping 4096 KB (one page) in 1 bit, a single 32-bit integer maps 128 KB.
 * Mapping 1 MB requires 8 integers.
 */
#define FULL_128KB_AREA (0xFFFFFFFF)
#define FULL_32KB_AREA (0xFF)


/*
 * This bitfield maps 1 bit to 4 KB of the whole physical memory.
 * The mapping has no shifts: its bit[0] corresponds to the first KB of the
 * physical memory, as well as its N-th bit corresponds to the N-th pageframe.
 */

u32 pageframes_bitfield[8 * MAX_MEM_SIZE_IN_MB];

/*
 * HACK: just assume we have 128 MB as memory. In the future, this value has to
 * be somehow passed from the bootloader to the kernel.
 */
u32 memsize_in_mb = 128;
int pageframes_used;
static u32 last_index;

#define BITFIELD_ELEMS (memsize_in_mb * 8)

void init_pageframe_allocator(void)
{

#ifdef KERNEL_TEST
   bzero((void *)pageframes_bitfield, sizeof(pageframes_bitfield));
   last_index = 0;
   pageframes_used = 0;
#else
   /*
    * In the kernel, pageframes_bitfield is zeroed because it is in the BSS
    * and this function is called only ONCE. No point in clearing the bitfield.
    */
#endif

   init_paging_pageframe_allocator();
}

/*
 * Use this function only during the initialization, not as a way to allocate
 * physical memory.
 */
void mark_pageframes_as_reserved(uptr paddr, int mb_count)
{
   // paddr has to be MB-aligned.
   ASSERT((paddr & (MB - 1)) == 0);

   for (int i = 0; i < mb_count * 8; i++) {
      ASSERT(pageframes_bitfield[(paddr >> 3) + i] == 0);
      pageframes_bitfield[(paddr >> 3) + i] = FULL_128KB_AREA;
   }
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

   for (u32 i = 0; i < BITFIELD_ELEMS; i++) {

      if (pageframes_bitfield[last_index] != FULL_128KB_AREA)
         goto success;

      last_index = (last_index + 1) % BITFIELD_ELEMS;
   }

   return INVALID_PADDR; // Default case: failure.

   success:

   pageframes_used++;
   free_index = get_first_zero_bit_index(pageframes_bitfield[last_index]);
   pageframes_bitfield[last_index] |= (1 << free_index);
   return ((last_index << 17) + (free_index << PAGE_SHIFT));
}

uptr alloc_32_pageframes_aligned(void)
{
   for (u32 i = 0; i < BITFIELD_ELEMS; i++) {

      u32 *bf = pageframes_bitfield + last_index;

      if (!*bf) {
         *bf = FULL_128KB_AREA;
         pageframes_used += 32;
         return last_index << 17;
      }

      last_index = (last_index + 1) % BITFIELD_ELEMS;
   }

   return INVALID_PADDR; // Default case: failure.
}


uptr alloc_32_pageframes(void)
{
   uptr paddr = alloc_32_pageframes_aligned();
   u8 *bf;

   if (paddr != INVALID_PADDR || get_free_pageframes_count() < 32) {
      return paddr;
   }

   for (u32 i = 0; i < BITFIELD_ELEMS; i++) {

      if (LIKELY(last_index != (BITFIELD_ELEMS - 1))) {
         bf = ((u8 *) &pageframes_bitfield[last_index]) + 1;
         if (!*(u32 *)bf) goto success;   // check with +1
         if (!*(u32 *)++bf) goto success; // check with +2
         if (!*(u32 *)++bf) goto success; // check with +3
      }

      last_index = (last_index + 1) % BITFIELD_ELEMS;
   }

   return INVALID_PADDR; // Default case: failure.

   success:
   *(u32 *)bf = FULL_128KB_AREA;
   pageframes_used += 32;
   return (bf - (u8 *) pageframes_bitfield) << (PAGE_SHIFT + 3);
}


uptr alloc_8_pageframes(void)
{
   u8 *bf;

   if (get_free_pageframes_count() < 8)
      return INVALID_PADDR;

   for (u32 i = 0; i < BITFIELD_ELEMS; i++) {

      bf = ((u8 *) &pageframes_bitfield[last_index]);
      if (!*bf) goto success;     // check with +0
      if (!*++bf) goto success;   // check with +1
      if (!*++bf) goto success;   // check with +2
      if (!*++bf) goto success;   // check with +3

      last_index = (last_index + 1) % BITFIELD_ELEMS;
   }

   return INVALID_PADDR;  // Default case: failure.

   success:
   *bf = FULL_32KB_AREA;
   pageframes_used += 8;
   return (bf - (u8 *) pageframes_bitfield) << (PAGE_SHIFT + 3);
}

void free_8_pageframes(uptr paddr)
{
   u32 byte_offset = paddr >> (PAGE_SHIFT + 3);
   ASSERT(*((u8 *)pageframes_bitfield + byte_offset) == FULL_32KB_AREA);
   *((u8 *)pageframes_bitfield + byte_offset) = 0;
   pageframes_used -= 8;
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
   last_index = majorIndex % BITFIELD_ELEMS;

   pageframes_used--;
}


bool is_allocated_pageframe(uptr address)
{
   uptr naddr = address & PAGE_MASK;
   u32 bitIndex = (naddr >> PAGE_SHIFT) & 31;
   u32 majorIndex = (naddr & 0xFFFE0000U) >> 17;
   return !!(pageframes_bitfield[majorIndex] & (1 << bitIndex));
}
