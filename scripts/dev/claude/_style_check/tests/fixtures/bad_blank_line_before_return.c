/* SPDX-License-Identifier: BSD-2-Clause */
#include <tilck/common/basic_defs.h>

int check_range(int lo, int hi, int val)
{
   int a = val - lo;
   int b = hi - val;
   int c = a + b;
   int d = c * 2;
   return a >= 0 && b >= 0;
}
