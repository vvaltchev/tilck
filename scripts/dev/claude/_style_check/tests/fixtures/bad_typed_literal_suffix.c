/* SPDX-License-Identifier: BSD-2-Clause */

typedef unsigned int u32;
typedef unsigned long long u64;

void demo(void)
{
   const u32 mask = (u32)0x1000;        /* cast of literal */
   const u64 big  = (u64)0xDEADBEEF;    /* cast of literal */

   (void)mask;
   (void)big;
}
