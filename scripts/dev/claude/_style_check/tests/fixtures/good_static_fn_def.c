/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Short static fn: return type/modifiers on their own line.
 * This is the project idiom even for short signatures.
 */
static void
short_helper(int x)
{
   (void)x;
}

/*
 * Long static fn: args aligned under the opening paren (Style 1
 * for function definitions); type on its own line.
 */
static int
long_helper_with_many_arguments(int first_argument,
                                int second_argument,
                                int third_argument)
{
   return first_argument + second_argument + third_argument;
}

/* Non-static fn -- the rule should not consider these. */
int public_helper(int a, int b)
{
   return a + b;
}
