/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <3rd_party/bithacks.h>


CONSTEXPR static ALWAYS_INLINE ulong
pow2_round_up_at(ulong n, ulong pow2unit)
{
   return (n + pow2unit - 1) & -pow2unit;
}

CONSTEXPR static ALWAYS_INLINE u64
pow2_round_up_at64(u64 n, u64 pow2unit)
{
   return (n + pow2unit - 1) & -pow2unit;
}

CONSTEXPR static ALWAYS_INLINE ulong
div_round_up(ulong n, ulong unit)
{
   return ((n + unit - 1) / unit);
}

CONSTEXPR static ALWAYS_INLINE u64
div_round_up64(u64 n, u64 unit)
{
   return ((n + unit - 1) / unit);
}

CONSTEXPR static ALWAYS_INLINE ulong
round_up_at(ulong n, ulong unit)
{
   return div_round_up(n, unit) * unit;
}

CONSTEXPR static ALWAYS_INLINE u64
round_up_at64(u64 n, u64 unit)
{
   return div_round_up64(n, unit) * unit;
}

CONSTEXPR static ALWAYS_INLINE ulong
round_down_at(ulong n, ulong unit)
{
   return (n / unit) * unit;
}

CONSTEXPR static ALWAYS_INLINE u64
round_down_at64(u64 n, u64 unit)
{
   return (n / unit) * unit;
}

CONSTEXPR static ALWAYS_INLINE ulong
make_bitmask(ulong width)
{
   ulong w = (sizeof(ulong) * 8) - width;
   return (((ulong)-1) << w) >> w;
}


CONSTEXPR static inline u32
get_first_zero_bit_index32(u32 num)
{
   u32 i;
   ASSERT(num != ~0U);

   for (i = 0; i < 32; i++)
      if ((num & (1U << i)) == 0)
         break;

   return i;
}

CONSTEXPR static inline u32
get_first_set_bit_index32(u32 num)
{
   u32 i;
   ASSERT(num != 0);

   for (i = 0; i < 32; i++)
      if (num & (1U << i))
         break;

   return i;
}

CONSTEXPR static inline u32
get_first_zero_bit_index64(u64 num)
{
   u32 i;

   ASSERT(num != ~0U);

   for (i = 0; i < 64; i++)
      if ((num & (1ull << i)) == 0)
         break;

   return i;
}

CONSTEXPR static inline u32
get_first_set_bit_index64(u64 num)
{
   u32 i;
   ASSERT(num != 0);

   for (i = 0; i < 64; i++)
      if (num & (1ull << i))
         break;

   return i;
}

CONSTEXPR static inline u32
get_first_zero_bit_index_l(ulong num)
{
   u32 i;
   ASSERT(num != ~0U);

   for (i = 0; i < NBITS; i++)
      if ((num & (1UL << i)) == 0)
         break;

   return i;
}

CONSTEXPR static inline u32
get_first_set_bit_index_l(ulong num)
{
   u32 i;
   ASSERT(num != 0);

   for (i = 0; i < NBITS; i++)
      if (num & (1UL << i))
         break;

   return i;
}
