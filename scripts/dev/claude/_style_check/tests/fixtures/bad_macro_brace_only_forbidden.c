/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * V2 -- brace-only wrap, no do/while. Caller can omit `;` and the
 * macro invocation will look inconsistent with normal statements.
 */
#define SET_AND_BUMP(p, val) { \
   *(p) = (val);               \
   (p)++;                      \
}

int dummy(void)
{
   int x = 0;
   int *p = &x;

   SET_AND_BUMP(p, 1)   /* no semicolon at call site -- ugly */

   return x;
}
