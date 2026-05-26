/* SPDX-License-Identifier: BSD-2-Clause */

extern int check_a(int);
extern int check_b(int);
extern int check_c(int);

int eval(int x)
{
   if (check_a(x) &&
       check_b(x)   &&
       check_c(x))
   {
      return 1;
   }

   return 0;
}
