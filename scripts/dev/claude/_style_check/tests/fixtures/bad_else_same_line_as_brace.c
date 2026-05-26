/* SPDX-License-Identifier: BSD-2-Clause */

void foo(int x)
{
   if (x > 0) {
      x++;
   }
   else {            /* violation: else on its own line */
      x--;
   }

   if (x > 10) {
      x = 0;
   }
   else if (x > 5) { /* violation: else if on its own line */
      x = 1;
   }
}
