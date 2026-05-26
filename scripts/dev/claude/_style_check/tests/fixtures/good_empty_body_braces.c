/* SPDX-License-Identifier: BSD-2-Clause */

extern int poll_status(void);

void wait_a(void)
{
   /* Non-empty body -- rule should not fire. */
   while (poll_status() != 1) {
      /* do nothing */
   }
}

void wait_b(void)
{
   int i;

   /* Non-empty body. */
   for (i = 0; i < 10; i++) {
      poll_status();
   }
}

/*
 * `do { ... } while (cond);` -- the trailing `;` after the while-
 * condition is NOT a NULL_STMT body. The rule must not fire.
 */
void wait_c(void)
{
   do {
      poll_status();
   } while (poll_status() != 2);
}
