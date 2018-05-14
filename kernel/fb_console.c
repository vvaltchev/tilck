
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/term.h>
#include <exos/hal.h>

#define PSF2_FONT_MAGIC 0x864ab572

typedef struct {
    u32 magic;
    u32 version;          /* zero */
    u32 header_size;
    u32 flags;            /* 0 if there's no unicode table */
    u32 glyphs_count;
    u32 bytes_per_glyph;
    u32 height;          /* height in pixels */
    u32 width;           /* width in pixels */
} psf2_header;

extern char _binary_font_psf_start;

void dump_glyph(int n)
{
   psf2_header *h = (void *)&_binary_font_psf_start;
   char *data = (char *)h + h->header_size + h->bytes_per_glyph * n;
   ASSERT(!(h->width % 8));

   for (u32 row = 0; row < h->height; row++) {

      u8 d = *(u8 *)(data + row * (h->width >> 3));

      for (u32 bit = 0; bit < h->width; bit++) {

         u32 val = 1 << bit;
         if (d & val)
            term_write_char('#');
         else
            term_write_char('-');
      }

      term_write_char('\n');
   }
}

void dump_psf2_header(void)
{
   psf2_header *h = (void *)&_binary_font_psf_start;
   printk("magic: %p\n", h->magic);

   if (h->magic != PSF2_FONT_MAGIC)
      panic("Magic != PSF2\n");

   printk("header size: %u%s\n",
          h->header_size,
          h->header_size > sizeof(psf2_header) ? " > sizeof(psf2_header)" : "");
   printk("flags: %p\n", h->flags);
   printk("glyphs count: %u\n", h->glyphs_count);
   printk("bytes per glyph: %u\n", h->bytes_per_glyph);
   printk("font size: %u x %u\n", h->width, h->height);

   if (h->width % 8) {
      panic("Only fonts with width divisible by 8 are supported");
   }

   printk("---- dump glyph ----\n");
   dump_glyph('A');
}

