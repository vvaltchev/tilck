/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

#include "devshell.h"

void dump_coverage_files(void)
{
   int rc;
   void *buf;
   char fname[256];
   unsigned fsize;
   const int fn = tilck_get_num_gcov_files(); // this syscall cannot fail

   printf("** GCOV gcda files **\n");

   for (int i = 0; i < fn; i++) {

      rc = tilck_get_gcov_file_info(i, fname, sizeof(fname), &fsize);

      if (rc != 0) {
         printf("[ERROR] tilck_get_gcov_file_info() failed with %d\n", rc);
         exit(1);
      }

      buf = malloc(fsize);

      if (!buf) {
         printf("[ERROR] Out-of-memory\n");
         exit(1);
      }

      printf("\nfile: %s\n", fname);

      rc = tilck_get_gcov_file(i, buf);

      if (rc != 0) {
         printf("[ERROR] tilck_get_gcov_file() failed with %d\n", rc);
         exit(1);
      }

      unsigned *ptr = buf;
      for (unsigned w = 0; w < fsize / 4; w++) {

         if (w && !(w % 6))
            printf("\n");

         printf("0x%08x ", ptr[w]);
      }

      printf("\n");
      free(buf);
   }
}
