/* SPDX-License-Identifier: BSD-2-Clause */

extern int do_work(int a, int b, int c, int d);
extern int two_arg(int a, int b);

/*
 * Style 1: args aligned under the opening paren. The first arg
 * sits on the open-paren line.
 */
int caller_a(void)
{
   return do_work(111, 222,
                  333, 444);
}

/*
 * Style 2: open paren ends the first line, args at +3 indent,
 * `);` on its own line aligned with the wrapping statement.
 */
int caller_b(void)
{
   return do_work(
      111,
      222,
      333,
      444
   );
}

/* Single-line call -- never wrapped, must not trigger anything. */
int caller_c(void)
{
   return two_arg(10, 20);
}
