
/*
 * Following the same philosophy described in fpu_memcpy.c, we want this code
 * to be optimized even in debug builds.
 */
#pragma GCC optimize "-O3"

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>
#include <tilck/common/string_util.h>
#include <tilck/common/color_defs.h>

#include <tilck/kernel/fb_console.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/system_mmap.h>

#include "fb_int.h"

static uptr fb_paddr;
static u32 fb_pitch;
static u32 fb_width;
static u32 fb_height;
static u8 fb_bpp; /* bits per pixel */
static u32 fb_size;

static u32 fb_bytes_per_pixel;
static u32 fb_line_length;

static uptr fb_vaddr; /* != fb_real_vaddr when a shadow buffer is used */
static uptr fb_real_vaddr;

static u32 *fb_w8_char_scanlines;

void append_mem_region(memory_region_t r);

void set_framebuffer_info_from_mbi(multiboot_info_t *mbi)
{
   __use_framebuffer = true;

   fb_paddr = mbi->framebuffer_addr;
   fb_pitch = mbi->framebuffer_pitch;
   fb_width = mbi->framebuffer_width;
   fb_height = mbi->framebuffer_height;
   fb_bpp = mbi->framebuffer_bpp;

   fb_bytes_per_pixel = fb_bpp / 8;
   fb_line_length = fb_width * fb_bytes_per_pixel;
   fb_size = fb_pitch * fb_height;

   append_mem_region((memory_region_t) {
      .addr = fb_paddr,
      .len = fb_size,
      .type = MULTIBOOT_MEMORY_RESERVED,
      .extra = MEM_REG_EXTRA_FRAMEBUFFER
   });
}

bool fb_alloc_shadow_buffer(void)
{
   size_t size = round_up_at(fb_size, PAGE_SIZE);
   void *shadow_buf = kzmalloc(size);

   if (!shadow_buf)
      return false;

   // NOTE: making the shadow buffer WC makes the performance worse
   // set_pages_pat_wc(get_kernel_pdir(), shadow_buf, size);
   fb_vaddr = (uptr) shadow_buf;
   return true;
}

void fb_flush_lines(u32 y, u32 lines_count)
{
   if (fb_vaddr == fb_real_vaddr)
      return;

   // ASSUMPTION fb_pitch is ALWAYS divisible by 32

   fpu_memcpy256_nt((void *)(fb_real_vaddr + y * fb_pitch),
                    (void *)(fb_vaddr + y * fb_pitch),
                    (lines_count * fb_pitch) >> 5);
}

/* NOTE: it is required that: dst_y < src_y */
void fb_lines_shift_up(u32 src_y, u32 dst_y, u32 lines_count)
{
   // ASSUMPTION fb_pitch is ALWAYS divisible by 32

   fpu_context_begin();

   fpu_memcpy256_nt((void *)(fb_vaddr + fb_pitch * dst_y),
                    (void *)(fb_vaddr + fb_pitch * src_y),
                    (fb_pitch * lines_count) >> 5);

   fpu_context_end();
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

void fb_map_in_kernel_space(void)
{
   fb_real_vaddr = KERNEL_BASE_VA + (1024 - 64) * MB;
   fb_vaddr = fb_real_vaddr; /* here fb_vaddr == fb_real_vaddr */

   map_framebuffer(fb_paddr, fb_vaddr, fb_size);
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
            memset((void *)v, color, fb_line_length);
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

void fb_raw_perf_screen_redraw(u32 color)
{
   VERIFY(fb_bpp == 32);
   VERIFY(fb_pitch == fb_line_length);

   memset32((void *)fb_real_vaddr, color, (fb_pitch * fb_height) >> 2);
}

void fb_draw_cursor_raw(u32 ix, u32 iy, u32 color)
{
   psf2_header *h = fb_font_header;

   if (LIKELY(fb_bpp == 32)) {

      ix <<= 2;

      for (u32 y = iy; y < (iy + h->height); y++) {

         memset32((u32 *)(fb_vaddr + (fb_pitch * y) + ix),
                  color,
                  h->width);
      }

   } else {

      /*
       * Generic (but slower version)
       * NOTE: Optimizing for bbp != 32 is completely out of Tilck's goals.
       */

      for (u32 y = iy; y < (iy + h->height); y++)
         for (u32 x = ix; x < (ix + h->width); x++)
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

   psf2_header *h = fb_font_header;

   if (!h) {
      printk("debug_dump_glyph: fb_font_header == 0: are we in text mode?\n");
      return;
   }

   // ASSUMPTION: width is divisible by 8
   const u32 width_bytes = h->width >> 3;
   u8 *data = (u8 *)h + h->header_size + h->bytes_per_glyph * n;

   printk(NO_PREFIX "\nGlyph #%u:\n\n", n);

   for (u32 row = 0; row < h->height; row++) {
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

void fb_draw_char_failsafe(u32 x, u32 y, u16 e)
{
   psf2_header *h = fb_font_header;

   const u8 c = vgaentry_get_char(e);
   ASSERT(c < h->glyphs_count);

   const u32 fg = vga_rgb_colors[vgaentry_get_fg(e)];
   const u32 bg = vga_rgb_colors[vgaentry_get_bg(e)];

   // ASSUMPTION: width is divisible by 8
   const u32 width_bytes = h->width >> 3;

   u8 *data = (u8 *)h + h->header_size + h->bytes_per_glyph * c;

   for (u32 row = 0; row < h->height; row++) {
      for (u32 b = 0; b < width_bytes; b++) {

         u8 sl = data[b + width_bytes * row];

         for (u32 bit = 0; bit < 8; bit++)
            fb_draw_pixel(x + (b << 3) + 8 - bit - 1,
                          y + row,
                          (sl & (1 << bit)) ? fg : bg);
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

#define PSZ         4     /* pixel size = bbp/8 =  4 */
#define SL_COUNT  256     /* all possible 8-pixel scanlines */
#define SL_SIZE     8     /* scanline size: 8 pixels */
#define FG_COLORS  16     /* #fg colors */
#define BG_COLORS  16     /* #bg colors */

#define TOT_CHAR_SCANLINES_SIZE (PSZ * SL_COUNT * FG_COLORS * BG_COLORS * SL_SIZE)

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
   psf2_header *h = fb_font_header;

   const u8 c = vgaentry_get_char(e);
   const u32 width_bytes = h->width >> 3;

   ASSUME_WITHOUT_CHECK(!(h->width % 8));
   ASSUME_WITHOUT_CHECK(width_bytes == 1 || width_bytes == 2);
   ASSUME_WITHOUT_CHECK(h->height == 16 || h->height == 32);
   ASSUME_WITHOUT_CHECK(h->bytes_per_glyph == 16 || h->bytes_per_glyph == 64);

   void *vaddr = (void *)fb_vaddr + (fb_pitch * y) + (x << 2);
   u8 *d = (u8 *)h + h->header_size + h->bytes_per_glyph * c;
   const u32 c_off = (vgaentry_get_fg(e) << 15) + (vgaentry_get_bg(e) << 11);
   u32 *scanlines = &fb_w8_char_scanlines[c_off];

   if (width_bytes == 1)

      for (u32 r = 0; r < h->height; r++, d++, vaddr += fb_pitch)
         memcpy32(vaddr, &scanlines[*d << 3], SL_SIZE);

   else

      // width_bytes == 2

      for (u32 r = 0; r < h->height; r++, d+=2, vaddr += fb_pitch) {
         memcpy32(vaddr, &scanlines[d[0] << 3], SL_SIZE);
         memcpy32(vaddr + 32, &scanlines[d[1] << 3], SL_SIZE);
      }
}

void fb_draw_char_optimized_row(u32 y, u16 *entries, u32 count)
{
   const psf2_header *h = fb_font_header;
   const u8 *data_base = (u8 *)h + h->header_size;
   const uptr vaddr_base = fb_vaddr + (fb_pitch * y);

   // ASSUMPTION: SL_SIZE is 8
   const u32 width_bytes = h->width >> 3;

   ASSUME_WITHOUT_CHECK(!(h->width % 8));
   ASSUME_WITHOUT_CHECK(width_bytes == 1 || width_bytes == 2);
   ASSUME_WITHOUT_CHECK(h->height == 16 || h->height == 32);
   ASSUME_WITHOUT_CHECK(h->bytes_per_glyph == 16 || h->bytes_per_glyph == 64);

   const u32 w4_shift = h->width == 8 ? 2 + 3 : 2 + 4;
   const u32 bpg_shift = h->bytes_per_glyph == 16 ? 4 : 6;

   for (u32 ei = 0; ei < count; ei++) {

      const u16 e = entries[ei];
      const u32 c_off = (vgaentry_get_fg(e) << 15) + (vgaentry_get_bg(e) << 11);
      void *vaddr = (void *)vaddr_base + (ei << w4_shift);
      const u8 *d = &data_base[vgaentry_get_char(e) << bpg_shift];
      u32 *scanlines = &fb_w8_char_scanlines[c_off];

      if (width_bytes == 1)

         for (u32 r = 0; r < h->height; r++, d++, vaddr += fb_pitch)
            fpu_cpy_single_256_nt(vaddr, &scanlines[*d << 3]);

      else

         // width_bytes == 2

         for (u32 r = 0; r < h->height; r++, d+=2, vaddr += fb_pitch) {
            fpu_cpy_single_256_nt(vaddr, &scanlines[d[0] << 3]);
            fpu_cpy_single_256_nt(vaddr + 32, &scanlines[d[1] << 3]);
         }

   }
}
