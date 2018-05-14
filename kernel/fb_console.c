
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/fb_console.h>
#include <exos/term.h>
#include <exos/paging.h>
#include <exos/hal.h>

bool use_framebuffer;

uptr fb_addr;
u32 fb_pitch;
u32 fb_width;
u32 fb_height;
u8 fb_bpp;
u32 fb_size;

void set_framebuffer_info_from_mbi(multiboot_info_t *mbi)
{
   use_framebuffer = true;

   fb_addr = mbi->framebuffer_addr;
   fb_pitch = mbi->framebuffer_pitch;
   fb_width = mbi->framebuffer_width;
   fb_height = mbi->framebuffer_height;
   fb_bpp = mbi->framebuffer_bpp;
   fb_size = fb_pitch * fb_height;
}

// temp
void draw_something(void);

void init_framebuffer_console(void)
{
   map_pages(get_kernel_page_dir(),
             (void *)fb_addr,
             fb_addr,
             (fb_size/PAGE_SIZE) + 1,
             false,
             true);

   draw_something();

   while (true)
      halt();
}

u32 fb_make_color(int r, int g, int b)
{
   return (r << 16) | (g << 8) | b;
}

static ALWAYS_INLINE void fb_draw_pixel(int x, int y, u32 color)
{
   // ASSUMPTION: bpp is assumed to be == 32.
   *(volatile u32 *)(fb_addr + (fb_pitch * y) + (x << 2)) = color;
}

void fb_draw_char(u32 x, u32 y, u32 color, u32 c)
{
   u32 black = fb_make_color(0, 0, 0);
   psf2_header *h = (void *)&_binary_font_psf_start;
   ASSERT(c < h->glyphs_count);

   u8 *data = (u8 *)h + h->header_size + h->bytes_per_glyph * c;

   for (u32 row = 0; row < h->height; row++) {

      // ASSUMPTION: width is divisible by 8
      u8 d = *(data + row * (h->width >> 3));

      for (u32 bit = 0; bit < h->width; bit++)
         fb_draw_pixel(x + bit, y + row, d & (1 << bit) ? color : black);
   }
}

void draw_something(void)
{
   u32 black = fb_make_color(0, 0, 0);

   for (u32 y = 0; y < fb_height; y++)
      for (u32 x = 0; x < fb_width; x++)
         fb_draw_pixel(x, y, black);

   u32 red_val = fb_make_color(255, 0, 0);

   fb_draw_char(100, 100, red_val, 'A');

   // u32 white_val = my_make_color(255, 255, 255);
   // u32 green_val = my_make_color(0, 255, 0);
   // u32 blue_val = my_make_color(0, 0, 255);

   int iy = 300;
   int ix = 300;
   int w = 200;

   for (int y = iy; y < iy+20; y++)
      for (int x = ix; x < ix+w; x++)
         fb_draw_pixel(x, y, red_val);

}

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

