/* SPDX-License-Identifier: BSD-2-Clause */

#define UPPER_HEX 0xABCD
#define UPPER_PREFIX 0X1234

void foo(void)
{
   unsigned a = 0xDEADBEEF;
   unsigned b = 0xabcd;  /* ok: lowercase */
   (void)a;
   (void)b;
}
