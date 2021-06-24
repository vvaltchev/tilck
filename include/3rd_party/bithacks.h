#pragma once
#include <tilck/common/basic_defs.h>

/*
 * From: http://graphics.stanford.edu/~seander/bithacks.html
 * with custom adaptions by Vladislav K. Valtchev.
 *
 *
 * Original description in the page
 * ----------------------------------
 *
 * Bit Twiddling Hacks
 *
 * By Sean Eron Anderson
 * seander@cs.stanford.edu
 *
 *
 * Individually, the code snippets here are in the public domain
 * (unless otherwise noted) — feel free to use them however you please.
 * The aggregate collection and descriptions are (C) 1997-2005 Sean Eron
 * Anderson. The code and descriptions are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY and without even the implied
 * warranty of merchantability or fitness for a particular purpose.
 * As of May 5, 2005, all the code has been tested thoroughly. Thousands of
 * people have read it. Moreover, Professor Randal Bryant, the Dean of Computer
 * Science at Carnegie Mellon University, has personally tested almost
 * everything with his Uclid code verification system. What he hasn't tested,
 * I have checked against all possible inputs on a 32-bit machine. To the
 * first person to inform me of a legitimate bug in the code, I'll pay a bounty
 * of US$10 (by check or Paypal). If directed to a charity, I'll pay US$20.
 */

CONSTEXPR static ALWAYS_INLINE ulong
log2_for_power_of_2(ulong v)
{
   ulong r;

   r = (v & 0xAAAAAAAA) != 0;

#ifdef BITS64
   r |= (ulong)((v & 0xFFFFFFFF00000000ULL) != 0) << 5;
#endif

   r |= (ulong)((v & 0xFFFF0000) != 0) << 4;
   r |= (ulong)((v & 0xFF00FF00) != 0) << 3;
   r |= (ulong)((v & 0xF0F0F0F0) != 0) << 2;
   r |= (ulong)((v & 0xCCCCCCCC) != 0) << 1;

   return r;
}

CONSTEXPR static inline ulong
roundup_next_power_of_2(ulong v)
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

/*
 * Sign extend the `w`-bits wide value in `val`
 */
CONSTEXPR static ALWAYS_INLINE long
sign_extend(long val, ulong w)
{
   ulong m = (sizeof(long) * 8) - w;
   return (val << m) >> m;
}
