/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <zlib.h>
#include <3rd_party/base64.h>

#include "devshell.h"

static void write_buf_on_screen(void *buf, unsigned size)
{
   size_t b64_size;
   unsigned char *b64 = base64_encode(buf, size, &b64_size);

   if (!b64) {
      printf("[ERROR] base64_encode() failed\n");
      exit(1);
   }

   fwrite(b64, 1, b64_size, stdout);
   free(b64);
}

static void compress_buf(void *buf, unsigned buf_size,
                         void *zbuf, unsigned *zbuf_size)
{
   z_stream defstream;

   defstream.zalloc = Z_NULL;
   defstream.zfree = Z_NULL;
   defstream.opaque = Z_NULL;
   defstream.data_type = Z_BINARY;

   defstream.avail_in = buf_size;
   defstream.next_in = buf;
   defstream.avail_out = *zbuf_size;
   defstream.next_out = zbuf;

   deflateInit2(&defstream,
                Z_BEST_COMPRESSION,
                Z_DEFLATED,
                MAX_WBITS,
                MAX_MEM_LEVEL,
                Z_DEFAULT_STRATEGY);

   deflate(&defstream, Z_FINISH);
   deflateEnd(&defstream);

   *zbuf_size = defstream.total_out;
}

void dump_coverage_files(void)
{
   int rc;
   void *buf, *zbuf;
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

      assert((fsize % 4) == 0);

      buf = malloc(fsize);
      zbuf = malloc(fsize);

      if (!buf || !zbuf) {
         printf("[ERROR] Out-of-memory\n");
         exit(1);
      }

      printf("\nfile: %s\n", fname);

      rc = tilck_get_gcov_file(i, buf);

      if (rc != 0) {
         printf("[ERROR] tilck_get_gcov_file() failed with %d\n", rc);
         exit(1);
      }

      compress_buf(buf, fsize, zbuf, &fsize);
      write_buf_on_screen(zbuf, fsize);
      free(zbuf);
      free(buf);
   }
}
