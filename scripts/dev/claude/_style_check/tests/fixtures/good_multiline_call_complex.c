/* SPDX-License-Identifier: BSD-2-Clause */

extern int compute(int, int);
extern int two_arg(int, int);

/*
 * Multi-line call where one ARGUMENT spans multiple lines (a sub-
 * expression). The rule should skip this case (it is the legitimate
 * term_write-style shape and not Style 3).
 */
int compose(int x)
{
   return two_arg(x,
                  (x > 0)
                     ? compute(x, x + 1)
                     : compute(0, 0));
}

/* Call with two args on the SAME continuation line (style 1, packed). */
int packed_continuation(int x)
{
   return compute(x,
                  x + 1);
}
