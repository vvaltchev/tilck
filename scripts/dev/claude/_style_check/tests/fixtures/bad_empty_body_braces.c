/* SPDX-License-Identifier: BSD-2-Clause */

extern int poll_status(void);

void wait_a(void)
{
   while (poll_status() != 1);    /* bare `;` body -- violation 1 */
}

void wait_b(void)
{
   int i;

   for (i = 0; poll_status() != 2; i++);
                                  /* bare `;` body -- violation 2 */
}
