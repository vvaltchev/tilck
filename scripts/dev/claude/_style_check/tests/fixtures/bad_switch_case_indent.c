/* SPDX-License-Identifier: BSD-2-Clause */

int classify(int x)
{
   int rc;

   switch (x) {
   case 1:                  /* flush with switch -- violation 1 */
      rc = 100;
      break;
   case 2:                  /* flush with switch -- violation 2 */
      rc = 200;
      break;
   default:                 /* flush with switch -- violation 3 */
      rc = 0;
      break;
   }

   return rc;
}
