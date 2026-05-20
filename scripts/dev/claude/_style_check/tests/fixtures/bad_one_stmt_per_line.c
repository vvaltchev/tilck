/* SPDX-License-Identifier: BSD-2-Clause */

extern int close(int);

void foo(void)
{
   close(0); close(1);     /* two statements -- violation */
   close(2);               /* ok */

   for (int i = 0; i < 10; i++) {  /* for-loop semis are inside parens, OK */
      (void)i;
   }
}
