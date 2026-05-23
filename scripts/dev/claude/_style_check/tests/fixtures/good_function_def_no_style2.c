/* SPDX-License-Identifier: BSD-2-Clause */

extern int helper(int);

/* Single-line definition -- not wrapped, rule must not fire. */
int short_def(int x)
{
   return x + 1;
}

/*
 * Style 1 (only acceptable wrapped form for function definitions):
 * first arg on the open-paren line, subsequent args aligned beneath.
 */
int wrapped_in_style_1(int first_argument_with_long_name,
                       int second_argument_with_long_name,
                       int third_argument_with_long_name)
{
   return first_argument_with_long_name
        + second_argument_with_long_name
        + third_argument_with_long_name;
}

/*
 * Function declaration (not a definition) -- the rule is restricted
 * to definitions; this must not fire even if wrapped.
 */
int declared_only(
   int a,
   int b,
   int c
);

int call_helper(void)
{
   return helper(1) + wrapped_in_style_1(1, 2, 3) + short_def(0);
}
