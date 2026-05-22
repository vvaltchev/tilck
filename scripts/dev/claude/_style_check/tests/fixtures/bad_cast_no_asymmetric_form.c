/* SPDX-License-Identifier: BSD-2-Clause */

extern void *something_void(void);
extern int do_stuff(int *);

int caller(void)
{
   int *p;
   int *q;

   p = (int*) something_void();    /* asymmetric cast -- violation 1 */
   q = (char*) p;                  /* asymmetric cast -- violation 2 */
   return do_stuff(p) + do_stuff(q);
}
