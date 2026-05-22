/* SPDX-License-Identifier: BSD-2-Clause */

extern int check_a(int);
extern int check_b(int);
extern int check_c(int);

int eval(int x)
{
   if (check_a(x)
       && check_b(x)               /* violation 1: && starts the line */
       && check_c(x)) {            /* violation 2 */
      return 1;
   }

   if (check_a(x)
       || check_b(x)) {            /* violation 3: || starts the line */
      return 2;
   }

   return 0;
}
