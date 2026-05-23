/* SPDX-License-Identifier: BSD-2-Clause */

int classify(int x)
{
   int rc;

   switch (x) {
      case 1:
      case 2:
      case 3:
         rc = 1;
         break;
      default:
         rc = 0;
         break;
   }

   return rc;
}
