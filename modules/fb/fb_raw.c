/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Following the same philosophy described in fpu_memcpy.c, we want this code
 * to be optimized even in debug builds.
 */

#if defined(__GNUC__) && !defined(__clang__)
   #pragma GCC optimize "-O3"
#endif

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>
#include <tilck/common/string_util.h>
#include <tilck/common/color_defs.h>

#include <tilck/mods/fb_console.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/system_mmap.h>

#include "fb_int.h"

void append_mem_region(struct mem_region r);

static uptr fb_paddr;
static u32 fb_pitch;
static u32 fb_width;
static u32 fb_height;
static u8 fb_bpp; /* bits per pixel */

u8 fb_red_pos;
u8 fb_green_pos;
u8 fb_blue_pos;

u8 fb_red_mask_size;
u8 fb_green_mask_size;
u8 fb_blue_mask_size;

u32 fb_red_mask;
u32 fb_green_mask;
u32 fb_blue_mask;

u32 fb_size;
static u32 fb_bytes_per_pixel;
static u32 fb_line_length;

uptr fb_vaddr;
static u32 *fb_w8_char_scanlines;

u32 font_w;
u32 font_h;
static u32 font_width_bytes;
static u32 font_bytes_per_glyph;
static u8 *font_glyph_data;

#define DARK    (168 /* vga */ + 0)
#define BRIGHT  (252 /* vga */ + 0)

u32 vga_rgb_colors[16];

static inline u32 fb_make_color(u32 r, u32 g, u32 b)
{
   return ((r << fb_red_pos) & fb_red_mask) |
          ((g << fb_green_pos) & fb_green_mask) |
          ((b << fb_blue_pos) & fb_blue_mask);
}

u32 fb_get_res_x(void) { return fb_width; }
u32 fb_get_res_y(void) { return fb_height; }
u32 fb_get_bbp(void) { return fb_bpp; }
u32 fb_get_font_w(void) { return font_w; }
u32 fb_get_font_h(void) { return font_h; }

static void fb_init_colors(void)
{
   u32 *c = vga_rgb_colors;

   c[COLOR_BLACK]          = fb_make_color(0      , 0      , 0      );
   c[COLOR_BLUE]           = fb_make_color(0      , 0      , DARK+30);
   c[COLOR_GREEN]          = fb_make_color(0      , DARK   , 0      );
   c[COLOR_CYAN]           = fb_make_color(0      , DARK   , DARK   );
   c[COLOR_RED]            = fb_make_color(DARK   , 0      , 0      );
   c[COLOR_MAGENTA]        = fb_make_color(DARK   , 0      , DARK   );
   c[COLOR_YELLOW]         = fb_make_color(DARK   , DARK   , 0      );
   c[COLOR_WHITE]          = fb_make_color(DARK+25, DARK+25, DARK+25);
   c[COLOR_BRIGHT_BLACK]   = fb_make_color(DARK-40, DARK-40, DARK-40);
   c[COLOR_BRIGHT_BLUE]    = fb_make_color(0      , 0      , BRIGHT );
   c[COLOR_BRIGHT_GREEN]   = fb_make_color(0      , BRIGHT , 0      );
   c[COLOR_BRIGHT_CYAN]    = fb_make_color(0      , BRIGHT , BRIGHT );
   c[COLOR_BRIGHT_RED]     = fb_make_color(BRIGHT , 0      , 0      );
   c[COLOR_BRIGHT_MAGENTA] = fb_make_color(BRIGHT , 0      , BRIGHT );
   c[COLOR_BRIGHT_YELLOW]  = fb_make_color(BRIGHT , BRIGHT , 0      );
   c[COLOR_BRIGHT_WHITE]   = fb_make_color(BRIGHT , BRIGHT , BRIGHT );
}

/* Note: using these constants won't work on big endian machines */
#define PSF1_FONT_MAGIC      0x0436
#define PSF2_FONT_MAGIC  0x864ab572

void fb_set_font(void *font)
{
   struct {
      u16 magic;
      u8 mode;
      u8 bytes_per_glyph;
   } *h1 = font;

   struct {
      u32 magic;
      u32 version;          /* zero */
      u32 header_size;
      u32 flags;            /* 0 if there's no unicode table */
      u32 glyphs_count;
      u32 bytes_per_glyph;
      u32 height;           /* height in pixels */
      u32 width;            /* width in pixels */
   } *h2 = font;

   if (h2->magic == PSF2_FONT_MAGIC) {

      font_w = h2->width;
      font_h = h2->height;
      font_width_bytes = h2->bytes_per_glyph / font_h;
      font_glyph_data = (u8 *)h2 + h2->header_size;
      font_bytes_per_glyph = h2->bytes_per_glyph;

   } else {

      VERIFY(h1->magic == PSF1_FONT_MAGIC);

      font_w = 8;
      font_h = h1->bytes_per_glyph;
      font_width_bytes = 1;
      font_glyph_data = (u8 *)h1 + sizeof(*h1);
      font_bytes_per_glyph = h1->bytes_per_glyph;
   }
}

void set_framebuffer_info_from_mbi(multiboot_info_t *mbi)
{
   __use_framebuffer = true;

   fb_paddr = (uptr) mbi->framebuffer_addr;
   fb_pitch = mbi->framebuffer_pitch;
   fb_width = mbi->framebuffer_width;
   fb_height = mbi->framebuffer_height;
   fb_bpp = mbi->framebuffer_bpp;

   fb_red_pos = mbi->framebuffer_red_field_position;
   fb_red_mask_size = mbi->framebuffer_red_mask_size;
   fb_red_mask = ((1u << fb_red_mask_size) - 1) << fb_red_pos;

   fb_green_pos = mbi->framebuffer_green_field_position;
   fb_green_mask_size = mbi->framebuffer_green_mask_size;
   fb_green_mask = ((1u << fb_green_mask_size) - 1) << fb_green_pos;

   fb_blue_pos = mbi->framebuffer_blue_field_position;
   fb_blue_mask_size = mbi->framebuffer_blue_mask_size;
   fb_blue_mask = ((1u << fb_blue_mask_size) - 1) << fb_blue_pos;

   //printk("red   [pos: %2u, mask: %p]\n", fb_red_pos, fb_red_mask);
   //printk("green [pos: %2u, mask: %p]\n", fb_green_pos, fb_green_mask);
   //printk("blue  [pos: %2u, mask: %p]\n", fb_blue_pos, fb_blue_mask);

   fb_bytes_per_pixel = fb_bpp / 8;
   fb_line_length = fb_width * fb_bytes_per_pixel;
   fb_size = fb_pitch * fb_height;

   fb_init_colors();

   append_mem_region((struct mem_region) {
      .addr = fb_paddr,
      .len = fb_size,
      .type = MULTIBOOT_MEMORY_RESERVED,
      .extra = MEM_REG_EXTRA_FRAMEBUFFER,
   });
}

void fb_lines_shift_up(u32 src_y, u32 dst_y, u32 lines_count)
{
   memcpy32((void *)(fb_vaddr + fb_pitch * dst_y),
            (void *)(fb_vaddr + fb_pitch * src_y),
            (fb_pitch * lines_count) >> 2);
}

u32 fb_get_width(void)
{
   return fb_width;
}

u32 fb_get_height(void)
{
   return fb_height;
}

u32 fb_get_bpp(void)
{
   return fb_bpp;
}

void fb_user_mmap(void *vaddr, size_t mmap_len)
{
   map_framebuffer(fb_paddr, (uptr)vaddr, mmap_len, true);
}

void fb_map_in_kernel_space(void)
{
   fb_vaddr = (uptr) map_framebuffer(fb_paddr, 0, fb_size, false);
}

/*
 * This function is used only by the failsafe functions: normally, there are
 * faster ways to draw on-screen than using a pixel by pixel method.
 */
static inline void fb_draw_pixel(u32 x, u32 y, u32 color)
{
   ASSERT(x < fb_width);
   ASSERT(y < fb_height);

   if (fb_bpp == 32) {

      *(volatile u32 *)
         (fb_vaddr + (fb_pitch * y) + (x << 2)) = color;

   } else {

      // Assumption: bpp is 24
      memcpy((void *) (fb_vaddr + (fb_pitch * y) + (x * 3)), &color, 3);
   }
}

void fb_raw_color_lines(u32 iy, u32 h, u32 color)
{
   if (LIKELY(fb_bpp == 32)) {

      uptr v = fb_vaddr + (fb_pitch * iy);

      if (LIKELY(fb_pitch == fb_line_length)) {

         memset32((void *)v, color, (fb_pitch * h) >> 2);

      } else {

         for (u32 i = 0; i < h; i++, v += fb_pitch)
            memset((void *)v, (int)color, fb_line_length);
      }

   } else {

      /*
       * Generic (but slower version)
       * NOTE: Optimizing for bbp != 32 is completely out of Tilck's goals.
       */

      for (u32 y = iy; y < (iy + h); y++)
         for (u32 x = 0; x < fb_width; x++)
            fb_draw_pixel(x, y, color);
   }
}

void fb_raw_perf_screen_redraw(u32 color, bool use_fpu)
{
   VERIFY(fb_bpp == 32);
   VERIFY(fb_pitch == fb_line_length);

   if (!use_fpu) {
      memset32((void *)fb_vaddr, color, (fb_pitch * fb_height) >> 2);
      return;
   }

   fpu_context_begin();
   {
      fpu_memset256((void *)fb_vaddr, color, (fb_pitch * fb_height) >> 5);
   }
   fpu_context_end();
}

void fb_draw_cursor_raw(u32 ix, u32 iy, u32 color)
{
   if (LIKELY(fb_bpp == 32)) {

      ix <<= 2;

      for (u32 y = iy; y < (iy + font_h); y++) {

         memset32((u32 *)(fb_vaddr + (fb_pitch * y) + ix),
                  color,
                  font_w);
      }

   } else {

      /*
       * Generic (but slower version)
       * NOTE: Optimizing for bbp != 32 is completely out of Tilck's goals.
       */

      for (u32 y = iy; y < (iy + font_h); y++)
         for (u32 x = ix; x < (ix + font_w); x++)
            fb_draw_pixel(x, y, color);
   }
}

void fb_copy_from_screen(u32 ix, u32 iy, u32 w, u32 h, u32 *buf)
{
   uptr vaddr = fb_vaddr + (fb_pitch * iy) + (ix * fb_bytes_per_pixel);

   if (LIKELY(fb_bpp == 32)) {

      for (u32 y = 0; y < h; y++, vaddr += fb_pitch)
         memcpy32(&buf[y * w], (void *)vaddr, w);

   } else {

      /*
       * Generic (but slower version)
       * NOTE: Optimizing for bbp != 32 is completely out of Tilck's goals.
       */

      for (u32 y = 0; y < h; y++, vaddr += fb_pitch)
         memcpy((u8 *)buf + y * w * fb_bytes_per_pixel,
                (void *)vaddr,
                w * fb_bytes_per_pixel);
   }
}

void fb_copy_to_screen(u32 ix, u32 iy, u32 w, u32 h, u32 *buf)
{
   uptr vaddr = fb_vaddr + (fb_pitch * iy) + (ix * fb_bytes_per_pixel);

   if (LIKELY(fb_bpp == 32)) {

      for (u32 y = 0; y < h; y++, vaddr += fb_pitch)
         memcpy32((void *)vaddr, &buf[y * w], w);

   } else {

      /*
       * Generic (but slower version)
       * NOTE: Optimizing for bbp != 32 is completely out of Tilck's goals.
       */

      for (u32 y = 0; y < h; y++, vaddr += fb_pitch)
         memcpy((void *)vaddr,
                (u8 *)buf + y * w * fb_bytes_per_pixel,
                w * fb_bytes_per_pixel);
   }
}

#ifdef DEBUG

void debug_dump_glyph(u32 n)
{
   static const char fgbg[2] = {'-', '#'};

   if (!font_glyph_data) {
      printk("debug_dump_glyph: font_glyph_data == 0: are we in text mode?\n");
      return;
   }

   // ASSUMPTION: width is divisible by 8
   const u32 width_bytes = font_w >> 3;
   u8 *data = font_glyph_data + font_bytes_per_glyph * n;

   printk(NO_PREFIX "\nGlyph #%u:\n\n", n);

   for (u32 row = 0; row < font_h; row++) {
      for (u32 b = 0; b < width_bytes; b++) {

         u8 sl = data[b + width_bytes * row];

         for (int bit = 7; bit >= 0; bit--) {
            printk(NO_PREFIX "%c", fgbg[!!(sl & (1 << bit))]);
         }
      }

      printk(NO_PREFIX "\n");
   }

   printk(NO_PREFIX "\n");
}

#endif

#define draw_char_partial(b)                                               \
   do {                                                                    \
      fb_draw_pixel(x + (b << 3) + 7, row, arr[!(data[b] & (1 << 0))]);    \
      fb_draw_pixel(x + (b << 3) + 6, row, arr[!(data[b] & (1 << 1))]);    \
      fb_draw_pixel(x + (b << 3) + 5, row, arr[!(data[b] & (1 << 2))]);    \
      fb_draw_pixel(x + (b << 3) + 4, row, arr[!(data[b] & (1 << 3))]);    \
      fb_draw_pixel(x + (b << 3) + 3, row, arr[!(data[b] & (1 << 4))]);    \
      fb_draw_pixel(x + (b << 3) + 2, row, arr[!(data[b] & (1 << 5))]);    \
      fb_draw_pixel(x + (b << 3) + 1, row, arr[!(data[b] & (1 << 6))]);    \
      fb_draw_pixel(x + (b << 3) + 0, row, arr[!(data[b] & (1 << 7))]);    \
   } while (0)

void fb_draw_char_failsafe(u32 x, u32 y, u16 e)
{
   u8 *data = font_glyph_data + font_bytes_per_glyph * vgaentry_get_char(e);

   u32 arr[] = {
      vga_rgb_colors[vgaentry_get_fg(e)],
      vga_rgb_colors[vgaentry_get_bg(e)],
   };

   /*
    * PERFORMANCE NOTE: using the following if (...) else if (...) sequence is
    * measurably faster on modern CPUs compared to turning the whole
    * fb_draw_char_failsafe() into a function pointer and having a separate
    * function per case because of the branch prediction.
    */

   if (LIKELY(font_width_bytes == 1))

      for (u32 row = y; row < (y + font_h); row++, data += font_width_bytes) {
         draw_char_partial(0);
      }

   else if (font_width_bytes == 2)

      for (u32 row = y; row < (y + font_h); row++, data += font_width_bytes) {
         draw_char_partial(0);
         draw_char_partial(1);
      }

   else

      for (u32 row = y; row < (y + font_h); row++, data += font_width_bytes) {
         for (u32 b = 0; b < font_width_bytes; b++) {
            draw_char_partial(b);
         }
      }
}


/*
 * -------------------------------------------
 *
 * Optimized funcs
 *
 * -------------------------------------------
 */

#define PSZ         4     /* pixel size = 32 bbp / 8 = 4 bytes */
#define SL_COUNT  256     /* all possible 8-pixel scanlines */
#define SL_SIZE     8     /* scanline size: 8 pixels */
#define FG_COLORS  16     /* #fg colors */
#define BG_COLORS  16     /* #bg colors */

#define TOT_CHAR_SCANLINES_SIZE (PSZ*SL_COUNT*FG_COLORS*BG_COLORS*SL_SIZE)

bool fb_pre_render_char_scanlines(void)
{
   fb_w8_char_scanlines = kmalloc(TOT_CHAR_SCANLINES_SIZE);

   if (!fb_w8_char_scanlines)
      return false;

   for (u32 fg = 0; fg < FG_COLORS; fg++) {
      for (u32 bg = 0; bg < BG_COLORS; bg++) {
         for (u32 sl = 0; sl < SL_COUNT; sl++) {
            for (u32 pix = 0; pix < SL_SIZE; pix++) {
               fb_w8_char_scanlines[
                  fg * (BG_COLORS * SL_COUNT * SL_SIZE) +
                  bg * (SL_COUNT * SL_SIZE) +
                  sl * SL_SIZE +
                  (SL_SIZE - pix - 1)
               ] = (sl & (1 << pix)) ? vga_rgb_colors[fg] : vga_rgb_colors[bg];
            }
         }
      }
   }

   return true;
}


void fb_draw_char_optimized(u32 x, u32 y, u16 e)
{
   const u8 c = vgaentry_get_char(e);
   const u32 width_bytes = font_w >> 3;

   ASSUME_WITHOUT_CHECK(!(font_w % 8));
   ASSUME_WITHOUT_CHECK(width_bytes == 1 || width_bytes == 2);
   ASSUME_WITHOUT_CHECK(font_h == 16 || font_h == 32);
   ASSUME_WITHOUT_CHECK(font_bytes_per_glyph==16 || font_bytes_per_glyph==64);

   void *vaddr = (void *)fb_vaddr + (fb_pitch * y) + (x << 2);
   u8 *d = font_glyph_data + font_bytes_per_glyph * c;
   const u32 c_off = (u32)(
      (vgaentry_get_fg(e) << 15) + (vgaentry_get_bg(e) << 11)
   );
   u32 *scanlines = &fb_w8_char_scanlines[c_off];

   if (width_bytes == 1)

      for (u32 r = 0; r < font_h; r++, d++, vaddr += fb_pitch)
         memcpy32(vaddr, &scanlines[*d << 3], SL_SIZE);

   else

      // width_bytes == 2

      for (u32 r = 0; r < font_h; r++, d+=2, vaddr += fb_pitch) {
         memcpy32(vaddr, &scanlines[d[0] << 3], SL_SIZE);
         memcpy32(vaddr + 32, &scanlines[d[1] << 3], SL_SIZE);
      }
}

void fb_draw_char_optimized_row(u32 y, u16 *entries, u32 count)
{
   const uptr vaddr_base = fb_vaddr + (fb_pitch * y);

   // ASSUMPTION: SL_SIZE is 8
   const u32 width_bytes = font_w >> 3;

   ASSUME_WITHOUT_CHECK(!(font_w % 8));
   ASSUME_WITHOUT_CHECK(width_bytes == 1 || width_bytes == 2);
   ASSUME_WITHOUT_CHECK(font_h == 16 || font_h == 32);
   ASSUME_WITHOUT_CHECK(font_bytes_per_glyph==16 || font_bytes_per_glyph==64);

   const u32 w4_shift = font_w == 8 ? 2 + 3 : 2 + 4;
   const u32 bpg_shift = font_bytes_per_glyph == 16 ? 4 : 6;

   for (u32 ei = 0; ei < count; ei++) {

      const u16 e = entries[ei];
      const u32 c_off = (u32) (
         (vgaentry_get_fg(e) << 15) + (vgaentry_get_bg(e) << 11)
      );
      void *vaddr = (void *)vaddr_base + (ei << w4_shift);
      const u8 *d = &font_glyph_data[vgaentry_get_char(e) << bpg_shift];
      u32 *scanlines = &fb_w8_char_scanlines[c_off];

      if (width_bytes == 1)

         for (u32 r = 0; r < font_h; r++, d++, vaddr += fb_pitch)
            fpu_cpy_single_256_nt(vaddr, &scanlines[*d << 3]);

      else

         // width_bytes == 2

         for (u32 r = 0; r < font_h; r++, d+=2, vaddr += fb_pitch) {
            fpu_cpy_single_256_nt(vaddr, &scanlines[d[0] << 3]);
            fpu_cpy_single_256_nt(vaddr + 32, &scanlines[d[1] << 3]);
         }

   }
}


#include <linux/fb.h>

void fb_fill_fix_info(void *fix_info)
{
   struct fb_fix_screeninfo *fi = fix_info;
   bzero(fi, sizeof(*fi));

   memcpy(fi->id, "fbdev", 5);
   fi->smem_start = fb_paddr;
   fi->smem_len = fb_size;
   fi->line_length = fb_pitch;
}

void fb_fill_var_info(void *var_info)
{
   struct fb_var_screeninfo *vi = var_info;
   bzero(vi, sizeof(*vi));

   vi->xres = fb_width;
   vi->yres = fb_height;
   vi->xres_virtual = fb_width;
   vi->yres_virtual = fb_height;
   vi->bits_per_pixel = fb_bpp;

   vi->red.offset = fb_red_pos;
   vi->red.length = fb_red_mask_size;
   vi->green.offset = fb_green_pos;
   vi->green.length = fb_green_mask_size;
   vi->blue.offset = fb_blue_pos;
   vi->blue.length = fb_blue_mask_size;

   // NOTE: vi->{red, green, blue}.msb_right = 0
}
