
#pragma once

#include <common_defs.h>

/*
* From: http://graphics.stanford.edu/~seander/bithacks.html
* with custom adaptions.
*/

static inline int CONSTEXPR log2_for_power_of_2(uptr v)
{
   static const uptr b[] = {
      0xAAAAAAAA
      , 0xCCCCCCCC
      , 0xF0F0F0F0
      , 0xFF00FF00
      , 0xFFFF0000

#ifdef BITS64
      , 0xFFFFFFFF00000000ULL
#endif
   };

   int i;
   register uptr r = (v & b[0]) != 0;


#ifdef BITS32
   for (i = 4; i > 0; i--) {
      r |= ((v & b[i]) != 0) << i;
   }
#else
   for (i = 5; i > 0; i--) {
      r |= ((v & b[i]) != 0) << i;
   }
#endif

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
