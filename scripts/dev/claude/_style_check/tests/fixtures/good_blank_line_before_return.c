/* SPDX-License-Identifier: BSD-2-Clause */
#include <tilck/common/basic_defs.h>

int complex_return(int a, int b, int c)
{
   int x = a + b;
   int y = b + c;
   int z = x + y;
   int w = z * 2;

   return x > 0 && y > 0;
}

int small_func(int a, int b)
{
   int x = a + b;
   return x > 0 && b > 0;
}
