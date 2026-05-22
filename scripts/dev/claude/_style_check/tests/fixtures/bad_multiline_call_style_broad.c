/* SPDX-License-Identifier: BSD-2-Clause */

extern int do_work(int a, int b, int c);

int caller(void)
{
   return do_work(
      111, 222, 333);                  /* Style 3 (broad form) -- violation */
}
