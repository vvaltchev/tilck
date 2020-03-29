/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#define PSF1_MAGIC               0x0436
#define PSF2_MAGIC               0x864ab572

struct psf1_header {
   uint16_t magic;
   uint8_t mode;
   uint8_t bytes_per_glyph;
};

struct psf2_header {
   uint32_t magic;
   uint32_t version;
   uint32_t header_size;
   uint32_t flags;
   uint32_t glyphs_count;
   uint32_t bytes_per_glyph;
   uint32_t height;          /* height in pixels */
   uint32_t width;           /* width in pixels */
};

static int font_fd = -1;
static size_t font_file_sz;
static void *font;
static uint32_t font_w;
static uint32_t font_h;
static uint32_t font_w_bytes;
static uint32_t font_bytes_per_glyph;
static uint8_t *font_data;

static int pbm_fd = -1;
static size_t pbm_file_sz;
static void *pbm;
static void *pbm_data;
static int pbm_w;
static int pbm_h;
static int pbm_w_bytes;
static int rows;
static int cols;

static int
open_and_mmap_file(const char *f, void **buf_ref, int *fd_ref, size_t *sz_ref)
{
   struct stat statbuf;
   int rc, fd = open(f, O_RDONLY);

   if (fd < 0)
      return -errno;

   if (fstat(fd, &statbuf) < 0)
      goto err_end;

   *buf_ref = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);

   if (*buf_ref == (void *)-1)
      goto err_end;

   *sz_ref = statbuf.st_size;
   *fd_ref = fd;
   return 0;

err_end:
   rc = -errno;
   close(fd);
   return rc;
}

static int
parse_font_file(void)
{
   struct psf1_header *h1 = font;
   struct psf2_header *h2 = font;

   if (h2->magic == PSF2_MAGIC) {
      font = h2;
      font_w = h2->width;
      font_h = h2->height;
      font_w_bytes = h2->bytes_per_glyph / h2->height;
      font_data = font + h2->header_size;
      font_bytes_per_glyph = h2->bytes_per_glyph;
   } else if (h1->magic == PSF1_MAGIC) {
      font = h1;
      font_w = 8;
      font_h = h1->bytes_per_glyph;
      font_w_bytes = 1;
      font_data = font + sizeof(struct psf1_header);
      font_bytes_per_glyph = h1->bytes_per_glyph;
   } else {
      return -1;
   }

   fprintf(stderr,
           "Detected %s font: ",
           h2->magic == PSF2_MAGIC ? "PSF2" : "PSF1");

   fprintf(stderr, "%d x %d\n", font_w, font_h);

   if (font_w != 8 && font_w != 16)
      return -1; /* font width not supported */

   return 0;
}

static int
parse_pbm_file(void)
{
   char type[32];
   char wstr[16], hstr[16];
   size_t i, n;

   sscanf(pbm, "%31s %15s %15s", type, wstr, hstr);

   if (strcmp(type, "P4"))
      return -1;

   pbm_w = atoi(wstr);
   pbm_h = atoi(hstr);

   if (!pbm_w || !pbm_h)
      return -1;

   rows = pbm_h / font_h;
   cols = pbm_w / font_w;

   for (i = 0, n = 0; i < pbm_file_sz && n < 2; i++) {
      if (((char *)pbm)[i] == 10)
         n++;
   }

   if (i == pbm_file_sz)
      return -1; /* corrupted PBM file */

   pbm_data = (char *)pbm + i;
   pbm_w_bytes = (pbm_w + 7) / 8;
   fprintf(stderr, "Detected PBM image: %d x %d\n", pbm_w, pbm_h);
   fprintf(stderr, "Screen size: %d x %d\n", cols, rows);
   return 0;
}

static char
recognize_ascii_char_at(int r, int c)
{
   uint8_t *img = pbm_data + pbm_w_bytes * font_h * r + font_w_bytes * c;

   for (int i = 32; i < 128; i++) {

      int y;
      uint8_t *g = (void *)(font_data + font_bytes_per_glyph * i);

      if (font_w_bytes == 1) {

         for (y = 0; y < font_h; y++) {
            if (img[y * pbm_w_bytes] != g[y])
               break;
         }

      } else {

         for (y = 0; y < font_h; y++) {

            if (img[y * pbm_w_bytes + 0] != g[y * 2 + 0])
               break;

            if (img[y * pbm_w_bytes + 1] != g[y * 2 + 1])
               break;
         }
      }

      if (y == font_h)
         return i;
   }

   return '?';
}

int main(int argc, char **argv)
{
   int rc;

   if (argc < 3) {
      fprintf(stderr, "Usage:\n");
      fprintf(stderr, "    %s <psf_font_file> <pbm_screenshot>\n", argv[0]);
      return 1;
   }

   if ((rc = open_and_mmap_file(argv[1], &font, &font_fd, &font_file_sz)) < 0) {
      fprintf(stderr, "ERROR: unable to open and mmap '%s': %s\n",
              argv[1], strerror(errno));

      return 1;
   }

   if ((rc = open_and_mmap_file(argv[2], &pbm, &pbm_fd, &pbm_file_sz)) < 0) {
      fprintf(stderr, "ERROR: unable to open and mmap '%s': %s\n",
              argv[2], strerror(errno));

      return 1;
   }

   if (parse_font_file() < 0) {
      fprintf(stderr, "ERROR: invalid font file\n");
      close(font_fd);
      close(pbm_fd);
      return 1;
   }

   if (parse_pbm_file() < 0) {
      fprintf(stderr, "ERROR: invalid PBM file. It must have P4 as magic");
      return 1;
   }

   putchar('+');

   for (int c = 0; c < cols; c++)
      putchar('-');

   putchar('+');
   putchar('\n');

   for (int r = 0; r < rows; r++) {

      putchar('|');

      for (int c = 0; c < cols; c++)
         putchar(recognize_ascii_char_at(r, c));

      putchar('|');
      putchar('\n');
   }

   putchar('+');

   for (int c = 0; c < cols; c++)
      putchar('-');

   putchar('+');
   putchar('\n');

   close(font_fd);
   close(pbm_fd);
   return 0;
}
