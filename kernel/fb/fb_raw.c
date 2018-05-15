
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/arch/generic_x86/vga_textmode_defs.h>

#include <exos/fb_console.h>
#include <exos/paging.h>
#include <exos/kmalloc.h>

#include "fb_int.h"

static uptr fb_paddr;
static u32 fb_pitch;
static u32 fb_width;
static u32 fb_height;
static u8 fb_bpp;
static u32 fb_size;
static uptr fb_vaddr;

static u32 *fb_w8_char_scanlines;

void set_framebuffer_info_from_mbi(multiboot_info_t *mbi)
{
   use_framebuffer = true;

   fb_paddr = mbi->framebuffer_addr;
   fb_pitch = mbi->framebuffer_pitch;
   fb_width = mbi->framebuffer_width;
   fb_height = mbi->framebuffer_height;
   fb_bpp = mbi->framebuffer_bpp;
   fb_size = fb_pitch * fb_height;
}

u32 fb_get_width(void)
{
   return fb_width;
}

u32 fb_get_height(void)
{
   return fb_height;
}

void fb_map_in_kernel_space(void)
{
   fb_vaddr = KERNEL_BASE_VA + (1024 - 64) * MB;

   map_pages(get_kernel_page_dir(),
             (void *)fb_vaddr,
             fb_paddr,
             (fb_size/PAGE_SIZE) + 1,
             false,
             true);
}


#define PSZ         4     /* pixel size = bbp/8 =  4 */
#define SL_COUNT  256     /* all possible 8-pixel scanlines */
#define SL_SIZE     8     /* scanline size: 8 pixels */
#define FG_COLORS  16     /* #fg colors */
#define BG_COLORS  16     /* #bg colors */

#define TOT_CHAR_SCANLINES_SIZE (PSZ * SL_COUNT * FG_COLORS * BG_COLORS * SL_SIZE)

void fb_precompute_fb_w8_char_scanlines(void)
{
   psf2_header *h = (void *)&_binary_font_psf_start;
   ASSERT(h->width == SL_SIZE);

   fb_w8_char_scanlines = kmalloc(TOT_CHAR_SCANLINES_SIZE);
   VERIFY(fb_w8_char_scanlines != NULL);

   for (u32 sl = 0; sl < SL_COUNT; sl++) {
      for (u32 fg = 0; fg < FG_COLORS; fg++) {
         for (u32 bg = 0; bg < BG_COLORS; bg++) {
            for (u32 pixel = 0; pixel < SL_SIZE; pixel++) {

               fb_w8_char_scanlines[
                  sl * (FG_COLORS * BG_COLORS * SL_SIZE) +
                  fg * (BG_COLORS * SL_SIZE) +
                  bg * SL_SIZE +
                  (SL_SIZE - pixel - 1)
               ] = (sl & (1 << pixel))
                  ? vga_rgb_colors[fg]
                  : vga_rgb_colors[bg];
            }
         }
      }
   }
}

void fb_raw_color_lines(u32 iy, u32 h, u32 color)
{
   // Assumption bbp is 32
   memset32((void *)(fb_vaddr + (fb_pitch * iy)), color, (fb_pitch * h) >> 2);
}

static ALWAYS_INLINE void fb_draw_pixel(u32 x, u32 y, u32 color)
{
   ASSERT(x < fb_width);
   ASSERT(y < fb_height);

   // ASSUMPTION: bpp is assumed to be == 32.
   *(volatile u32 *)(fb_vaddr + (fb_pitch * y) + (x << 2)) = color;
}

void fb_draw_cursor_raw(u32 ix, u32 iy, u32 color)
{
   psf2_header *h = (void *)&_binary_font_psf_start;
   ix <<= 2; // Assumption: bbp is 32

   for (u32 y = 0; y < h->height; y++) {

      memset32((u32 *)(fb_vaddr + (fb_pitch * (iy + y)) + ix),
               color,
               h->width);
   }
}

void fb_draw_char_raw(u32 x, u32 y, u16 entry)
{
   psf2_header *h = (void *)&_binary_font_psf_start;

   const u8 c = vgaentry_char(entry);
   const u8 color = vgaentry_color(entry);
   const u32 fg = vga_rgb_colors[vgaentry_color_fg(color)];
   const u32 bg = vga_rgb_colors[vgaentry_color_bg(color)];

   ASSERT(c < h->glyphs_count);

   // ASSUMPTION: width is divisible by 8
   const u32 width_div_8 = h->width >> 3;

   u8 *data = (u8 *)h + h->header_size + h->bytes_per_glyph * c;

   for (u32 row = 0; row < h->height; row++) {

      u8 sl = *(data + row * width_div_8);

      for (u32 bit = 0; bit < h->width; bit++)
         fb_draw_pixel(x + h->width - bit - 1,
                       y + row,
                       (sl & (1 << bit)) ? fg : bg);
   }
}

/* fg and bg here are VGA colors [0..15] */
void fb_draw_w8_char_raw(u32 x, u32 y, u16 entry)
{
   psf2_header *h = (void *)&_binary_font_psf_start;

   /* h->width must be 8 */

   const u8 c = vgaentry_char(entry);
   const u8 color = vgaentry_color(entry);
   const u8 fg = vgaentry_color_fg(color);
   const u8 bg = vgaentry_color_bg(color);

   u8 *data = (u8 *)h + h->header_size + h->bytes_per_glyph * c;

   for (u32 row = 0; row < h->height; row++) {

      const u8 sl = data[row];
      const u32 offset = (sl << 11) + (fg << 7) + (bg << 3);

      memcpy((void *)(fb_vaddr + (fb_pitch * (y + row)) + (x << 2)),
             &fb_w8_char_scanlines[offset],
             PSZ * SL_SIZE);
   }
}

void fb_copy_from_screen(u32 ix, u32 iy, u32 w, u32 h, u32 *buf)
{
   const u32 ix4 = ix << 2;
   const u32 w4 = w << 2;

   for (u32 y = 0; y < h; y++) {
      memcpy(&buf[y * w],
             (void *)(fb_vaddr + (fb_pitch * (iy + y)) + ix4),
             w4);
   }
}

void fb_copy_to_screen(u32 ix, u32 iy, u32 w, u32 h, u32 *buf)
{
   const u32 ix4 = ix << 2;
   const u32 w4 = w << 2;

   for (u32 y = 0; y < h; y++) {
      memcpy((void *)(fb_vaddr + (fb_pitch * (iy + y)) + ix4),
             &buf[y * w],
             w4);
   }
}
