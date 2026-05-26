/* SPDX-License-Identifier: BSD-2-Clause */

int compute(int x)
{
   int a = x + 1;
   int b = x + 2;
   int c = x + 3;
   x = a + b + c;             /* no blank line between decls and code */
   return x * 2;
}
