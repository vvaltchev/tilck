/* SPDX-License-Identifier: BSD-2-Clause */

extern int do_work(int);

int compute(int x)
{
   int a;

   a = do_work(x);
   int b;                     /* mid-block non-const decl -- violation 1 */
   b = a + 1;
   int c = b * 2;             /* mid-block non-const decl -- violation 2 */
   return c;
}
