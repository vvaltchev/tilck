/* SPDX-License-Identifier: BSD-2-Clause */

int classify(int x)
{
   int rc;

   switch (x) {
      case 1:
         {
            int tmp = x + 1;     /* OK: braced sub-block */
            rc = tmp;
         }
         break;
      case 2:
         int tmp2 = x + 2;       /* un-braced case-body decl -- violation */
         rc = tmp2;
         break;
      default:
         rc = 0;
         break;
   }

   return rc;
}
