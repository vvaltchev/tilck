/* SPDX-License-Identifier: BSD-2-Clause */
#include <tilck/common/basic_defs.h>

int foo(ulong x)
{
   int a = (int)x;
   long b = (long)a;
   return (int)(b + (long)a);
}
