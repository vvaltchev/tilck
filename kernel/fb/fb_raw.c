
/*
 * Following the same philosophy described in fpu_memcpy.c, we want this code
 * to be optimized even in debug builds.
 */
#pragma GCC optimize "-O3"

#include <exos/common/basic_defs.h>
#include <exos/common/utils.h>
#include <exos/common/string_util.h>
#include <exos/common/color_defs.h>

#include <exos/kernel/fb_console.h>
#include <exos/kernel/paging.h>
#include <exos/kernel/kmalloc.h>
#include <exos/kernel/hal.h>

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
static void *fb_pitch_size_buf;

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

bool fb_alloc_pitch_size_buf(void)
{
   fb_pitch_size_buf = kmalloc(fb_pitch);
   return fb_pitch_size_buf != NULL;
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

   fpu_memcpy256_nt((void *)(fb_real_vaddr + y * fb_pitch),
                    (void *)(fb_vaddr + y * fb_pitch),
                    (lines_count * fb_pitch) >> 5);
}

/* NOTE: it is required that: dst_y < src_y */
void fb_lines_shift_up(u32 src_y, u32 dst_y, u32 lines_count)
{
   // ASSUMPTION fb_pitch is ALWAYS divisible by 32

   fpu_context_begin();

   if (!fb_pitch_size_buf) {
      fpu_memcpy256_nt((void *)(fb_vaddr + fb_pitch * dst_y),
                       (void *)(fb_vaddr + fb_pitch * src_y),
                       (fb_pitch * lines_count) >> 5);
      goto out;
   }

   for (u32 i = 0; i < lines_count; i++) {

      fpu_memcpy256_nt_read(fb_pitch_size_buf,
                            (void *)(fb_vaddr + fb_pitch * (src_y + i)),
                            fb_pitch >> 5);

      if (x86_cpu_features.can_use_sse2)
         asmVolatile("mfence");

      fpu_memcpy256_nt((void *)(fb_vaddr + fb_pitch * (dst_y + i)),
                       fb_pitch_size_buf,
                       fb_pitch >> 5);
   }

out:
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

#ifdef __i386__
#include "../arch/i386/paging_int.h"
#endif

void fb_map_in_kernel_space(void)
{
   fb_real_vaddr = KERNEL_BASE_VA + (1024 - 64) * MB;
   fb_vaddr = fb_real_vaddr; /* here fb_vaddr == fb_real_vaddr */

#ifdef __i386__
   if (!get_kernel_page_dir()) {

      /*
       * Paging has not been initialized yet: probably we're in panic.
       * At this point, the kernel still uses page_size_buf as pdir, with only
       * the first 4 MB of the physical mapped at KERNEL_BASE_VA.
       */

      kernel_page_dir = (page_directory_t *)page_size_buf;

      u32 big_pages_to_use = fb_size / (4 * MB) + 1;

      for (u32 i = 0; i < big_pages_to_use; i++) {
         map_4mb_page_int(kernel_page_dir,
                          (void *)fb_real_vaddr + i * 4 * MB,
                          fb_paddr + i * 4 * MB,
                          PG_PRESENT_BIT | PG_RW_BIT | PG_4MB_BIT);
      }

      return;
   }
#endif

   int page_count = (fb_size / PAGE_SIZE) + 1;

   int rc = map_pages(get_kernel_page_dir(),
                      (void *)fb_real_vaddr,
                      fb_paddr,
                      page_count,
                      false,
                      true);

   if (rc < page_count)
      panic("Unable to map the framebuffer in the virtual space");
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

   if (fb_bpp != 32) {
      // Generic (but slower version)
      for (u32 y = 0; y < h; y++, vaddr += fb_pitch)
         memcpy((u8 *)buf + y * w * fb_bytes_per_pixel,
                (void *)vaddr,
                w * fb_bytes_per_pixel);
      return;
   }

   for (u32 y = 0; y < h; y++, vaddr += fb_pitch)
      memcpy32(&buf[y * w], (void *)vaddr, w);
}

void fb_copy_to_screen(u32 ix, u32 iy, u32 w, u32 h, u32 *buf)
{
   uptr vaddr = fb_vaddr + (fb_pitch * iy) + (ix * fb_bytes_per_pixel);

   if (fb_bpp != 32) {
      // Generic (but slower version)
      for (u32 y = 0; y < h; y++, vaddr += fb_pitch)
         memcpy((void *)vaddr,
                (u8 *)buf + y * w * fb_bytes_per_pixel,
                w * fb_bytes_per_pixel);
      return;
   }

   for (u32 y = 0; y < h; y++, vaddr += fb_pitch)
      memcpy32((void *)vaddr, &buf[y * w], w);
}

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

