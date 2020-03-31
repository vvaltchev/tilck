/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
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

static bool opt_border = true;
static bool opt_quiet = false;

static char (*recognize_char)(int, int);
static char recognize_ascii_char_at_w8(int r, int c);
static char recognize_ascii_char_at_w16(int r, int c);

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

   if (!opt_quiet) {
      fprintf(stderr,
            "Detected %s font: ",
            h2->magic == PSF2_MAGIC ? "PSF2" : "PSF1");

      fprintf(stderr, "%d x %d\n", font_w, font_h);
   }

   if (font_w != 8 && font_w != 16)
      return -1; /* font width not supported */

   if (font_w == 8)
      recognize_char = &recognize_ascii_char_at_w8;
   else
      recognize_char = &recognize_ascii_char_at_w16;

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

   if (!opt_quiet) {
      fprintf(stderr, "Detected PBM image: %d x %d\n", pbm_w, pbm_h);
      fprintf(stderr, "Screen size: %d x %d\n", cols, rows);
   }

   return 0;
}

static char
recognize_ascii_char_at_w8(int r, int c)
{
   int i, y;
   uint8_t *img = pbm_data + pbm_w_bytes * font_h * r + font_w_bytes * c;

   for (i = 32, y = 0; i < 128 && y < font_h; i++) {

      uint8_t *g = (void *)(font_data + font_bytes_per_glyph * i);

      for (y = 0; y < font_h; y++)
         if (img[y * pbm_w_bytes] != g[y])
            break;
   }

   return i < 128 ? i-1 : '?';
}

static char
recognize_ascii_char_at_w16(int r, int c)
{
   uint16_t *img = pbm_data + pbm_w_bytes * font_h * r + font_w_bytes * c;
   const int pbm_w_bytes_half = pbm_w_bytes / 2;
   int i, y;

   for (i = 32, y = 0; i < 128 && y < font_h; i++) {

      uint16_t *g = (void *)(font_data + font_bytes_per_glyph * i);

      for (y = 0; y < font_h; y++)
         if (img[y * pbm_w_bytes_half] != g[y])
            break;
   }

   return i < 128 ? i-1 : '?';
}

static void
show_help(FILE *fh)
{
   fprintf(fh, "Usage:\n");
   fprintf(fh, "    pbm2text [-nq] <psf_font> <pbm_screenshot>\n\n");
   fprintf(fh, "Options:\n");
   fprintf(fh, "    -n    Don't print any border\n");
   fprintf(fh, "    -q    Quiet: no info messages\n");
}

static void
show_help_and_exit(void)
{
   show_help(stderr);
   exit(1);
}

static void
print_hline(void)
{
   putchar('+');

   for (int c = 0; c < cols; c++)
      putchar('-');

   putchar('+');
   putchar('\n');
}

static void
parse_and_dump_screen_with_border(void)
{
   print_hline();

   for (int r = 0; r < rows; r++) {

      putchar('|');

      for (int c = 0; c < cols; c++)
         putchar(recognize_char(r, c));

      putchar('|');
      putchar('\n');
   }

   print_hline();
}

int main(int argc, char **argv)
{
   int rc;

   while (argc > 1 && argv[1][0] == '-') {

      if (!strcmp(argv[1], "-n")) {
         opt_border = false;
      } else if (!strcmp(argv[1], "-q")) {
         opt_quiet = true;
      } else if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
         show_help(stdout);
         return 0;
      } else {
         fprintf(stderr, "ERROR: unknown option '%s'\n", argv[1]);
         show_help_and_exit();
      }

      argc--; argv++;
   }

   if (argc < 3)
      show_help_and_exit();

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

   if (opt_border) {

      parse_and_dump_screen_with_border();

   } else {

      for (int r = 0; r < rows; r++) {

         for (int c = 0; c < cols; c++)
            putchar(recognize_char(r, c));

         putchar('\n');
      }
   }

   close(font_fd);
   close(pbm_fd);
   return 0;
}
