/* SPDX-License-Identifier: BSD-2-Clause */

int compute(void);

void foo(int param)
{
   (void)compute();    /* discard return: violation */
   (void)param;        /* silence unused warning: also violation */
}
