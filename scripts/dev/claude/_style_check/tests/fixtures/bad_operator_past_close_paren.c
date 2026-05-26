/* SPDX-License-Identifier: BSD-2-Clause */

extern int short_check(void);
extern int much_longer_condition_predicate(int x);

int eval(int x)
{
   /*
    * `&&` on the wrapped lines sits at the natural end-of-cond
    * column (~col 22), but the final closing `)` lands at col ~52
    * because the last condition is much longer. The operators
    * must be padded right so they sit PAST the closing paren's
    * column (Q25 refinement).
    */
   if (short_check() &&
       short_check() &&
       much_longer_condition_predicate(x))
   {
      return 1;
   }

   return 0;
}
