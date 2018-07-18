
#pragma once

#include <exos/common/basic_defs.h>

/*
* From: http://graphics.stanford.edu/~seander/bithacks.html
* with custom adaptions.
*/

static ALWAYS_INLINE int CONSTEXPR log2_for_power_of_2(uptr v)
{
   uptr r;

   r = (v & 0xAAAAAAAA) != 0;

#ifdef BITS64
   r |= ((v & 0xFFFFFFFF00000000ULL) != 0) << 5;
#endif

   r |= ((v & 0xFFFF0000) != 0) << 4;
   r |= ((v & 0xFF00FF00) != 0) << 3;
   r |= ((v & 0xF0F0F0F0) != 0) << 2;
   r |= ((v & 0xCCCCCCCC) != 0) << 1;

   return r;
}

/*
* From: http://graphics.stanford.edu/~seander/bithacks.html
* with custom adaptions.
*/

CONSTEXPR static inline uptr roundup_next_power_of_2(uptr v)
{
   v--;
   v |= v >> 1;
   v |= v >> 2;
   v |= v >> 4;
   v |= v >> 8;
   v |= v >> 16;

#ifdef BITS64
   v |= v >> 32;
#endif

   v++;

   return v;
}

uint32_t crc32(uint32_t crc, const void *buf, size_t size);

CONSTEXPR static inline u32 get_first_zero_bit_index(u32 num)
{
   u32 i;

   ASSERT(num != ~0U);

   for (i = 0; i < 32; i++) {
      if ((num & (1U << i)) == 0) break;
   }

   return i;
}

CONSTEXPR static inline u32 get_first_set_bit_index(u32 num)
{
   u32 i;
   ASSERT(num != 0);

   for (i = 0; i < 32; i++)
      if (num & (1U << i))
         break;

   return i;
}

CONSTEXPR static ALWAYS_INLINE uptr round_up_at(uptr n, uptr unit)
{
   return (n + unit - 1) & -unit;
}

CONSTEXPR static ALWAYS_INLINE u64 round_up_at64(u64 n, u64 unit)
{
   return (n + unit - 1) & -unit;
}
