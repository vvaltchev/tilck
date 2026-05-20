/* SPDX-License-Identifier: BSD-2-Clause */

extern int some_long_function_name(int, int, int);
extern int another_function(int, int, int);

static void
foo(void)
{
   /* Style 3 (violation): args on one indented line, ); at end */
   some_long_function_name(
      1, 2, 3);

   /* Style 2 (compliant): each arg on its own line, ); on its own line */
   another_function(
      1,
      2,
      3
   );
}
