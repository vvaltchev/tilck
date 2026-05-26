/* SPDX-License-Identifier: BSD-2-Clause */

extern int check_a(int);
extern int check_b(int);
extern int check_c(int);

int eval(int x)
{
   /* Operators aligned at the same column. */
   if (check_a(x)   &&
       check_b(x)   &&
       check_c(x))
   {
      return 1;
   }

   return 0;
}

int single_clause(int x)
{
   /* Single-line condition -- rule must not fire. */
   if (check_a(x) && check_b(x))
      return 1;

   return 0;
}
