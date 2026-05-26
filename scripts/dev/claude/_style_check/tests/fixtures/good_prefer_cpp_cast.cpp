/* SPDX-License-Identifier: BSD-2-Clause */

int compute(int x)
{
   int a = static_cast<int>(x);
   long b = static_cast<long>(a);
   char *p = reinterpret_cast<char *>(&a);
   if (a > 0) {
      return a;
   }
   return sizeof(int);
}
