/* SPDX-License-Identifier: BSD-2-Clause */

/*
intentionally malformed multi-line block comment (no leading asterisk)
on this line and the next
*/

#include <tilck/common/basic_defs.h>

/*
 * Violates cols_80 below (the line is intentionally padded with text to
 * push it well past 80 columns so the checker has something to flag here).
 */
static const char *very_long_string = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

/* Violates sizeof_parens: sizeof without parens */
static int
foo(int x)
{
   int n = sizeof x;
   return n;
}

/* Violates static_fn_def_type_own_line: wrapped sig with type on name line */
static int multi_line_sig_with_type_on_name_line(int aaaaaa,
                                                 int bbbbbb,
                                                 int cccccc)
{
   return aaaaaa + bbbbbb + cccccc;
}
