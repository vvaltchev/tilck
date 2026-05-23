/* SPDX-License-Identifier: BSD-2-Clause */

int classify(int x)
{
   int rc;

   switch (x) {
      case 1:
         rc = 100;
         break;
      case 2:
         rc = 200;
         break;
      default:
         rc = 0;
         break;
   }

   /* Nested switch -- the outer switch's rule must not measure
    * the inner switch's cases. */
   switch (rc) {
      case 100:
         {
            switch (x + 1) {
               case 2:
                  rc++;
                  break;
               default:
                  break;
            }
         }
         break;
      default:
         break;
   }

   return rc;
}
