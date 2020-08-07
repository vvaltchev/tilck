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
round_up_at(ulong n, ulong unit)
{
   return ((n + unit - 1) / unit) * unit;
}

CONSTEXPR static ALWAYS_INLINE u64
round_up_at64(u64 n, u64 unit)
{
   return ((n + unit - 1) / unit) * unit;
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
