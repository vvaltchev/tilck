/* SPDX-License-Identifier: BSD-2-Clause */

void foo(int x)
{
   if (x > 0) {
      x++;
   } else {
      x--;
   }

   if (x > 10) {
      x = 0;
   } else if (x > 5) {
      x = 1;
   } else {
      x = 2;
   }
}
