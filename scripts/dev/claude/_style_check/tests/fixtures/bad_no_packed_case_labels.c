/* SPDX-License-Identifier: BSD-2-Clause */

int classify(int x)
{
   int rc;

   switch (x) {
      case 1: case 2:                /* packed labels -- violation */
         rc = 12;
         break;
      case 3: case 4: case 5:        /* packed labels -- 2 violations */
         rc = 345;
         break;
      default:
         rc = 0;
         break;
   }

   return rc;
}
