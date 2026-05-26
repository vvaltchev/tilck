/* SPDX-License-Identifier: BSD-2-Clause */
#include <tilck/common/basic_defs.h>

void foo(ulong x, u64 y, u8 z)
{
   ulong a = x + 1;
   u64 b = y + 2;
   u8 c = z + 3;
   (void)a;
   (void)b;
   (void)c;
}
