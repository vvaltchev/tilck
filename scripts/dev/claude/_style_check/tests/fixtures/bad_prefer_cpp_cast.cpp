/* SPDX-License-Identifier: BSD-2-Clause */

int compute(int x)
{
   int a = (int)x;
   long b = (long)a;
   char *p = (char *)&a;
   return a;
}
