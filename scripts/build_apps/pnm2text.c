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

/*
 * NOTE: PNM stands for: portable anymap format (PNM).
 * This tool supports only PBM (monochrome) and PPM (24 bpp).
 */

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

static int pnm_fd = -1;
static size_t pnm_file_sz;
static void *pnm;
static void *pnm_data;
static int pnm_w;
static int pnm_h;
static int pnm_w_bytes;
static int pnm_w_bytes_half;
static int pnm_type;

static int rows;
static int cols;

static bool opt_border = true;
static bool opt_quiet = false;

static uint8_t (*recognize_char)(void *, int, int);
static void (*img_get_char)(int, int, char *);

static void img_p4_get_char8(int row, int col, char *dest);
static void img_p4_get_char16(int row, int col, char *dest);
static uint8_t recognize_char_at_w8(void *img_ptr, int r, int c);
static uint8_t recognize_char_at_w16(void *img_ptr, int r, int c);

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
      recognize_char = &recognize_char_at_w8;
   else
      recognize_char = &recognize_char_at_w16;

   return 0;
}

static int
parse_pnm_file(void)
{
   char type[32];
   char wstr[16], hstr[16];
   size_t i, n;

   sscanf(pnm, "%31s %15s %15s", type, wstr, hstr);

   if (!strcmp(type, "P4")) {
      pnm_type = 4;
   } else if (!strcmp(type, "P6")) {
      pnm_type = 6;
   } else {
      return -1;
   }

   pnm_w = atoi(wstr);
   pnm_h = atoi(hstr);

   if (!pnm_w || !pnm_h)
      return -1;

   rows = pnm_h / font_h;
   cols = pnm_w / font_w;

   for (i = 0, n = 0; i < pnm_file_sz && n < 2; i++) {
      if (((char *)pnm)[i] == 10)
         n++;
   }

   if (i == pnm_file_sz)
      return -1; /* corrupted pnm file */

   pnm_data = (char *)pnm + i;
   pnm_w_bytes = (pnm_w + 7) / 8;
   pnm_w_bytes_half = pnm_w_bytes / 2;

   if (!opt_quiet) {
      fprintf(stderr, "Detected %s image: %d x %d\n",
              pnm_type == 4 ? "PBM" : "PPM", pnm_w, pnm_h);
      fprintf(stderr, "Screen size: %d x %d\n", cols, rows);
   }

   return 0;
}

static uint8_t
recognize_char_at_w8(void *img_ptr, int r, int c)
{
   int i, y;
   uint8_t *img = img_ptr;

   for (i = 0, y = 0; i < 256 && y < font_h; i++) {

      uint8_t *g = (void *)(font_data + font_bytes_per_glyph * i);

      for (y = 0; y < font_h; y++)
         if (img[y] != g[y])
            break;

      if (y < font_h) {
         for (y = 0; y < font_h; y++)
            if ((uint8_t)~img[y] != g[y])
               break;
      }
   }

   return i < 256 ? i-1 : '?';
}

static uint8_t
recognize_char_at_w16(void *img_ptr, int r, int c)
{
   uint16_t *img = img_ptr;
   int i, y;

   for (i = 0, y = 0; i < 256 && y < font_h; i++) {

      uint16_t *g = (void *)(font_data + font_bytes_per_glyph * i);

      for (y = 0; y < font_h; y++)
         if (img[y] != g[y])
            break;

      if (y < font_h) {
         for (y = 0; y < font_h; y++)
            if ((uint16_t)~img[y] != g[y])
               break;
      }
   }

   return i < 256 ? i-1 : '?';
}

static const char transl[256] = {

   [  1] = '-',

   [176] = '#',
   [177] = '#',
   [179] = '|',
   [180] = '+',

   [191] = '+',
   [192] = '+',
   [193] = '+',
   [194] = '+',
   [195] = '+',
   [196] = '-',
   [197] = '+',

   [217] = '+',
   [218] = '+',
   [219] = '#',
   [223] = '+',
};

static void
img_p4_get_char8(int row, int col, char *dest)
{
   void *p = pnm_data + pnm_w_bytes * font_h * row + col;

   for (int y = 0; y < font_h; y++)
      ((uint8_t *)dest)[y] = ((uint8_t *)p)[y * pnm_w_bytes];
}

static void
img_p4_get_char16(int row, int col, char *dest)
{
   void *p = pnm_data + pnm_w_bytes * font_h * row + 2 * col;

   for (int y = 0; y < font_h; y++)
      ((uint16_t *)dest)[y] = ((uint16_t *)p)[y * pnm_w_bytes_half];
}

static void
recognize_and_print_char(int row, int col)
{
   char imgbuf[font_w_bytes * font_h]; // VLA

   img_get_char(row, col, imgbuf);
   uint8_t c = recognize_char(&imgbuf, row, col);

   putchar(
      32 <= c && c <= 127
         ? c
         : transl[c] ? transl[c] : '?'
   );
}

static void
show_help(FILE *fh)
{
   fprintf(fh, "Usage:\n");
   fprintf(fh, "    pnm2text [-nq] <psf_font> <pnm_screenshot>\n\n");
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
         recognize_and_print_char(r, c);

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

   if ((rc = open_and_mmap_file(argv[2], &pnm, &pnm_fd, &pnm_file_sz)) < 0) {
      fprintf(stderr, "ERROR: unable to open and mmap '%s': %s\n",
              argv[2], strerror(errno));
      return 1;
   }

   if (parse_font_file() < 0) {
      fprintf(stderr, "ERROR: invalid font file\n");
      close(font_fd);
      close(pnm_fd);
      return 1;
   }

   if (parse_pnm_file() < 0) {
      fprintf(stderr, "ERROR: invalid file. It must have P4 or P6 as type");
      return 1;
   }

   if (pnm_type == 4) {

      if (font_w_bytes == 1)
         img_get_char = &img_p4_get_char8;
      else
         img_get_char = &img_p4_get_char16;

   } else {

      /* png_type == "P6" */

      /* NOT IMPLEMENTED */
      return 1;
   }

   if (opt_border) {

      parse_and_dump_screen_with_border();

   } else {

      for (int r = 0; r < rows; r++) {

         for (int c = 0; c < cols; c++)
            recognize_and_print_char(r, c);

         putchar('\n');
      }
   }

   close(font_fd);
   close(pnm_fd);
   return 0;
}
