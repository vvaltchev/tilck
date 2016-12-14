
#pragma once

#include <commonDefs.h>

#if defined(__i386__) || defined(__x86_64__)

static ALWAYS_INLINE u64 RDTSC()
{

#ifdef BITS64
   uptr lo, hi;
   asm("rdtsc" : "=a" (lo), "=d" (hi));
   return lo | (hi << 32);
#else
   u64 val;
   asm("rdtsc" : "=A" (val));
   return val;
#endif
}

#endif
