/* SPDX-License-Identifier: BSD-2-Clause */

int classify(int x)
{
   int rc;

   if (x < 0)
      return -1;              /* violation: next line not blank */
   rc = x;

   if (x > 100)
      return 100;             /* violation: next line not blank */
   rc++;

   return rc;
}
