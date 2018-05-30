
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/vga_textmode_defs.h>


#include <exos/fb_console.h>
#include <exos/paging.h>
#include <exos/kmalloc.h>
#include <exos/pageframe_allocator.h>

#include "fb_int.h"

static uptr fb_paddr;
static u32 fb_pitch;
static u32 fb_width;
static u32 fb_height;
static u8 fb_bpp; /* bits per pixel */
static u32 fb_size;

static u32 fb_bytes_per_pixel;
static uptr fb_vaddr; /* != fb_real_vaddr when a shadow buffer is used */
static uptr fb_real_vaddr;

static u32 *fb_w8_char_scanlines;

void set_framebuffer_info_from_mbi(multiboot_info_t *mbi)
{
   __use_framebuffer = true;

   fb_paddr = mbi->framebuffer_addr;
   fb_pitch = mbi->framebuffer_pitch;
   fb_width = mbi->framebuffer_width;
   fb_height = mbi->framebuffer_height;
   fb_bpp = mbi->framebuffer_bpp;
   fb_bytes_per_pixel = fb_bpp / 8;
   fb_size = fb_pitch * fb_height;
}

bool fb_alloc_shadow_buffer(void)
{
   void *shadow_buf = kzmalloc(fb_size);

   if (!shadow_buf)
      return false;

   fb_vaddr = (uptr) shadow_buf;
   return true;
}

void fb_flush_lines(u32 y, u32 lines_count)
{
   if (fb_vaddr == fb_real_vaddr)
      return;

   // ASSUMPTION fb_pitch is ALWAYS divisible by 32

   // TODO: add fpu_save_context here

   fpu_memcpy256_nt((void *)(fb_real_vaddr + y * fb_pitch),
                    (void *)(fb_vaddr + y * fb_pitch),
                    (lines_count * fb_pitch) >> 5);

   // TODO: add fpu_restore_context here
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

   map_pages(get_kernel_page_dir(),
             (void *)fb_real_vaddr,
             fb_paddr,
             (fb_size / PAGE_SIZE) + 1,
             false,
             true);

   mark_pageframes_as_reserved(fb_paddr, (fb_size / MB) + 1);
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
   if (fb_bpp == 32) {
      memset32((void *)(fb_vaddr + (fb_pitch * iy)),
               color, (fb_pitch * h) >> 2);
   } else {

      // Generic (but slower version)
      for (u32 y = iy; y < (iy + h); y++)
         for (u32 x = 0; x < fb_width; x++)
            fb_draw_pixel(x, y, color);
   }
}

void fb_draw_cursor_raw(u32 ix, u32 iy, u32 color)
{
   psf2_header *h = fb_font_header;

   if (fb_bpp == 32) {

      ix <<= 2;

      for (u32 y = iy; y < (iy + h->height); y++) {

         memset32((u32 *)(fb_vaddr + (fb_pitch * y) + ix),
                  color,
                  h->width);
      }

   } else {

      // Generic (but slower version)
      for (u32 y = iy; y < (iy + h->height); y++)
         for (u32 x = ix; x < (ix + h->width); x++)
            fb_draw_pixel(x, y, color);
   }
}

void fb_copy_from_screen(u32 ix, u32 iy, u32 w, u32 h, u32 *buf)
{
   uptr vaddr = fb_vaddr + (fb_pitch * iy) + (ix * fb_bytes_per_pixel);

   if (fb_bpp == 32) {

      for (u32 y = 0; y < h; y++, vaddr += fb_pitch)
         memcpy32(&buf[y * w], (void *)vaddr, w);

      return;
   }

   // Generic (but slower version)
   for (u32 y = 0; y < h; y++, vaddr += fb_pitch)
      memcpy((u8 *)buf + y * w * fb_bytes_per_pixel,
             (void *)vaddr,
             w * fb_bytes_per_pixel);
}

void fb_copy_to_screen(u32 ix, u32 iy, u32 w, u32 h, u32 *buf)
{
   uptr vaddr = fb_vaddr + (fb_pitch * iy) + (ix * fb_bytes_per_pixel);

   if (fb_bpp == 32) {

       for (u32 y = 0; y < h; y++, vaddr += fb_pitch)
         memcpy32((void *)vaddr, &buf[y * w], w);

      return;
   }

   // Generic (but slower version)
   for (u32 y = 0; y < h; y++, vaddr += fb_pitch)
      memcpy((void *)vaddr,
             (u8 *)buf + y * w * fb_bytes_per_pixel,
             w * fb_bytes_per_pixel);
}

void fb_draw_char_failsafe(u32 x, u32 y, u16 e)
{
   psf2_header *h = fb_font_header;

   const u8 c = vgaentry_char(e);
   ASSERT(c < h->glyphs_count);

   const u32 fg = vga_rgb_colors[vgaentry_fg(e)];
   const u32 bg = vga_rgb_colors[vgaentry_bg(e)];

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

/* NOTE: it is required that: dst_y < src_y */
void fb_lines_shift_up(u32 src_y, u32 dst_y, u32 count)
{
   // ASSUMPTION fb_pitch is ALWAYS divisible by 32

   // TODO: add fpu_save_context here

   fpu_memcpy256_nt((void *)(fb_vaddr + fb_pitch * dst_y),
                    (void *)(fb_vaddr + fb_pitch * src_y),
                    (fb_pitch * count) >> 5);

   // TODO: add fpu_restore_context here
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

   const u8 c = vgaentry_char(e);
   ASSERT(c < h->glyphs_count);

   // ASSUMPTION: width is divisible by 8
   const u32 width_bytes = h->width >> 3;

   uptr vaddr = fb_vaddr + (fb_pitch * y) + (x << 2);
   u8 *data = (u8 *)h + h->header_size + h->bytes_per_glyph * c;
   const u32 c_off = (vgaentry_fg(e) << 15) + (vgaentry_bg(e) << 11);

   for (u32 row = 0; row < h->height; row++) {
      for (u32 b = 0; b < width_bytes; b++) {

         u32 sl = data[b + width_bytes * row];

         memcpy32((void *)(vaddr + (b << 5)),
                  &fb_w8_char_scanlines[c_off + (sl << 3)],
                  SL_SIZE);
      }
      vaddr += fb_pitch;
   }
}


void fb_draw_char16x32(u32 x, u32 y, u16 e)
{
   psf2_header *h = fb_font_header;

   const u8 c = vgaentry_char(e);
   uptr vaddr = fb_vaddr + (fb_pitch * y) + (x << 2);
   u8 *data = (u8 *)h + h->header_size + h->bytes_per_glyph * c;
   const u32 c_off = (vgaentry_fg(e) << 15) + (vgaentry_bg(e) << 11);

   for (u32 row = 0; row < 32; row++) {

      memcpy32((void *)(vaddr),
               &fb_w8_char_scanlines[c_off + (data[row << 1] << 3)],
               SL_SIZE);

      memcpy32((void *)(vaddr + 32),
               &fb_w8_char_scanlines[c_off + (data[1 + (row << 1)] << 3)],
               SL_SIZE);

      vaddr += fb_pitch;
   }
}

void fb_draw_char16x32_row(u32 y, u16 *entries, u32 count)
{
   const psf2_header *h = fb_font_header;
   const u8 *data = (u8 *)h + h->header_size;
   const uptr vaddr_base = fb_vaddr + (fb_pitch * y);

   /* fb_bpp must be 32 */
   /* h->width must be 16 */
   /* h->height must be 32 */
   /* h->bytes_per_glyph must be 16 */
   /* SL_SIZE is 8 */

   // TODO: add fpu_save_context here

   for (u32 ei = 0; ei < count; ei++) {

      const u16 e = entries[ei];
      const u32 c32 = vgaentry_char(e) << 6;
      const u32 c_off = (vgaentry_fg(e) << 15) + (vgaentry_bg(e) << 11);
      uptr vaddr = vaddr_base + (ei << 6);

      for (u32 row = 0; row < 32; row++) {

         fpu_memcpy_single_256_nt(
                  (void *)(vaddr),
                  &fb_w8_char_scanlines[c_off + (data[c32+(row << 1)] << 3)]);

         fpu_memcpy_single_256_nt(
                  (void *)(vaddr + 32),
                  &fb_w8_char_scanlines[c_off + (data[c32+1+(row << 1)] << 3)]);

         vaddr += fb_pitch;
      }
   }

   // TODO: add fpu_restore_context here
}

void fb_draw_char8x16(u32 x, u32 y, u16 e)
{
   psf2_header *h = fb_font_header;

   /* fb_bpp must be 32 */
   /* h->width must be 8 */
   /* h->height must be 16 */
   /* h->bytes_per_glyph must be 16 */

   u8 *data = (u8 *)h + h->header_size + (vgaentry_char(e) << 4);
   uptr vaddr = fb_vaddr + (fb_pitch * y) + (x << 2);
   const u32 c_off = (vgaentry_fg(e) << 15) + (vgaentry_bg(e) << 11);

   for (u32 r = 0; r < 16; r++) {

      memcpy32((void *)vaddr,
               &fb_w8_char_scanlines[c_off + (data[r] << 3)],
               SL_SIZE);

      vaddr += fb_pitch;
   }
}

void fb_draw_char8x16_row(u32 y, u16 *entries, u32 count)
{
   const psf2_header *h = fb_font_header;
   const u8 *data = (u8 *)h + h->header_size;
   const uptr vaddr_base = fb_vaddr + (fb_pitch * y);

   /* fb_bpp must be 32 */
   /* h->width must be 8 */
   /* h->height must be 16 */
   /* h->bytes_per_glyph must be 16 */

   // TODO: add fpu_save_context here

   for (u32 ei = 0; ei < count; ei++) {

      const u16 e = entries[ei];
      const u32 c16 = vgaentry_char(e) << 4;
      const u32 c_off = (vgaentry_fg(e) << 15) + (vgaentry_bg(e) << 11);

      uptr vaddr = vaddr_base + (ei << 5);

      for (u32 r = 0; r < 16; r++) {

         fpu_memcpy_single_256_nt(
            (void *)vaddr,
            &fb_w8_char_scanlines[c_off + (data[c16 + r] << 3)]);

         vaddr += fb_pitch;
      }
   }

   // TODO: add fpu_restore_context here
}

