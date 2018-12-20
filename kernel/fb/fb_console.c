/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/color_defs.h>

#include <tilck/kernel/fb_console.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/datetime.h>

#include "fb_int.h"

extern char _binary_font8x16_psf_start;
extern char _binary_font16x32_psf_start;

bool __use_framebuffer;
psf2_header *fb_font_header;

static bool use_optimized;
static u32 fb_term_rows;
static u32 fb_term_cols;
static u32 fb_offset_y;

static bool cursor_enabled;
static bool banner_refresh_disabled;
static int cursor_row;
static int cursor_col;
static u32 *under_cursor_buf;
static volatile bool cursor_visible = true;
static task_info *blink_thread_ti;
static const u32 blink_half_period = (TIMER_HZ * 45)/100;
static u32 cursor_color;

static video_interface framebuffer_vi;

void fb_save_under_cursor_buf(void)
{
   if (!under_cursor_buf)
      return;

   psf2_header *h = fb_font_header;
   const u32 ix = cursor_col * h->width;
   const u32 iy = fb_offset_y + cursor_row * h->height;
   fb_copy_from_screen(ix, iy, h->width, h->height, under_cursor_buf);
}

void fb_restore_under_cursor_buf(void)
{
   if (!under_cursor_buf)
      return;

   psf2_header *h = fb_font_header;
   const u32 ix = cursor_col * h->width;
   const u32 iy = fb_offset_y + cursor_row * h->height;
   fb_copy_to_screen(ix, iy, h->width, h->height, under_cursor_buf);
}

static void fb_reset_blink_timer(void)
{
   if (!blink_thread_ti)
      return;

   cursor_visible = true;
   task_update_wakeup_timer_if_any(blink_thread_ti, blink_half_period);
}

/* video_interface */

void fb_set_char_at_failsafe(int row, int col, u16 entry)
{
   psf2_header *h = fb_font_header;

   fb_draw_char_failsafe(col * h->width,
                         fb_offset_y + row * h->height,
                         entry);

   if (row == cursor_row && col == cursor_col)
      fb_save_under_cursor_buf();

   fb_reset_blink_timer();
}

void fb_set_char_at_optimized(int row, int col, u16 entry)
{
   psf2_header *h = fb_font_header;

   fb_draw_char_optimized(col * h->width,
                          fb_offset_y + row * h->height,
                          entry);

   if (row == cursor_row && col == cursor_col)
      fb_save_under_cursor_buf();

   fb_reset_blink_timer();
}

void fb_clear_row(int row_num, u8 color)
{
   psf2_header *h = fb_font_header;
   const u32 iy = fb_offset_y + row_num * h->height;
   fb_raw_color_lines(iy, h->height, vga_rgb_colors[get_color_bg(color)]);

   if (cursor_row == row_num)
      fb_save_under_cursor_buf();
}

void fb_move_cursor(int row, int col, int cursor_vga_color)
{
   if (!under_cursor_buf)
      return;

   psf2_header *h = fb_font_header;

   fb_restore_under_cursor_buf();

   if (row != cursor_row || col != cursor_col)
      cursor_visible = true;

   cursor_row = row;
   cursor_col = col;

   if (cursor_vga_color >= 0)
      cursor_color = vga_rgb_colors[cursor_vga_color];

   if (cursor_enabled) {

      fb_save_under_cursor_buf();

      if (cursor_visible) {
         fb_draw_cursor_raw(cursor_col * h->width,
                            fb_offset_y + cursor_row * h->height,
                            cursor_color);

         fb_reset_blink_timer();
      }
   }
}

void fb_enable_cursor(void)
{
   if (cursor_enabled)
      return;

   /*
    * The cursor was disabled and now we have to re-enable it again. In the
    * meanwhile many thing might happened, like the whole screen scrolled.
    * Therefore, before enabling the cursor, we have to update the
    * under_cursor_buf.
    */

   fb_save_under_cursor_buf();
   cursor_enabled = true;
   fb_move_cursor(cursor_row, cursor_col, -1);
}

void fb_disable_cursor(void)
{
   if (!cursor_enabled)
      return;

   cursor_enabled = false;
   fb_move_cursor(cursor_row, cursor_col, -1);
}

static void fb_set_row_failsafe(int row, u16 *data, bool flush)
{
   for (u32 i = 0; i < fb_term_cols; i++)
      fb_set_char_at_failsafe(row, i, data[i]);

   fb_reset_blink_timer();
}

static void fb_set_row_optimized(int row, u16 *data, bool flush)
{
   psf2_header *h = fb_font_header;

   fb_draw_char_optimized_row(fb_offset_y + row * h->height,
                              data,
                              fb_term_cols);

   fb_reset_blink_timer();
}

static void fb_draw_banner(void);

static void fb_disable_banner_refresh(void)
{
   banner_refresh_disabled = true;
}

static void fb_enable_banner_refresh(void)
{
   banner_refresh_disabled = false;
   fb_draw_banner();
}

// ---------------------------------------------

static video_interface framebuffer_vi =
{
   fb_set_char_at_failsafe,
   fb_set_row_failsafe,
   fb_clear_row,
   fb_move_cursor,
   fb_enable_cursor,
   fb_disable_cursor,
   NULL,  /* scroll_one_line_up: used only when running in a VM */
   NULL,  /* flush_buffers: never used by fb_console */
   fb_draw_banner,
   fb_disable_banner_refresh,
   fb_enable_banner_refresh
};


static void fb_blink_thread()
{
   while (true) {

      if (cursor_enabled) {
         cursor_visible = !cursor_visible;
         fb_move_cursor(cursor_row, cursor_col, -1);
      }

      kernel_sleep(blink_half_period);
   }
}

static void fb_draw_string_at_raw(u32 x, u32 y, const char *str, u8 color)
{
   psf2_header *h = fb_font_header;

   if (use_optimized)

      for (; *str; str++, x += h->width)
         fb_draw_char_optimized(x, y, make_vgaentry(*str, color));
   else

      for (; *str; str++, x += h->width)
         fb_draw_char_failsafe(x, y, make_vgaentry(*str, color));
}

static void fb_setup_banner(void)
{
   if (in_panic())
      return;

   psf2_header *h = fb_font_header;
   fb_offset_y = (20 * h->height)/10;
}

static void fb_draw_banner(void)
{
   static bool oom;
   static char *lbuf;
   static char *rbuf;

   if (oom)
      return;

   if (!lbuf) {

      lbuf = kmalloc(fb_term_cols + 1);

      if (!lbuf) {
         oom = true;
         return;
      }
   }

   if (!rbuf) {

      rbuf = kmalloc(fb_term_cols + 1);

      if (!rbuf) {
         oom = true;
         return;
      }
   }

   psf2_header *h = fb_font_header;
   int llen, rlen, padding, i;
   datetime_t d;

   ASSERT(fb_offset_y >= h->height);

   read_system_clock_datetime(&d);

   llen = snprintk(lbuf, fb_term_cols - 1,
                   "Tilck [%s build] framebuffer console", BUILDTYPE_STR);

   rlen = snprintk(rbuf, fb_term_cols - llen - 1,
                   "%02i %s %i %02i:%02i",
                   d.day, months3[d.month - 1],
                   d.year, d.hour, d.min);

   padding = (fb_term_cols - llen - rlen - 1);

   for (i = llen; i < llen + padding; i++)
      lbuf[i] = ' ';

   memcpy(lbuf + i, rbuf, rlen);
   lbuf[fb_term_cols - 1] = 0;

   fb_raw_color_lines(0, fb_offset_y, 0 /* black */);
   fb_raw_color_lines(fb_offset_y - 4, 1, vga_rgb_colors[COLOR_BRIGHT_WHITE]);
   fb_draw_string_at_raw(h->width/2, h->height/2, lbuf, COLOR_BRIGHT_YELLOW);

   u32 top_lines_used = fb_offset_y + h->height * fb_term_rows;

   fb_raw_color_lines(top_lines_used,
                      fb_get_height() - top_lines_used,
                      vga_rgb_colors[COLOR_BLACK]);
}

static void fb_update_banner_kthread()
{
   while (true) {

      if (!banner_refresh_disabled)
         fb_draw_banner();

      kernel_sleep(60 * TIMER_HZ);
   }
}

static void fb_scroll_one_line_up(void)
{
   psf2_header *h = fb_font_header;

   bool enabled = cursor_enabled;

   if (enabled)
     fb_disable_cursor();

   fb_lines_shift_up(fb_offset_y + h->height, /* source: row 1 (+ following) */
                     fb_offset_y,             /* destination: row 0 */
                     fb_get_height() - fb_offset_y - h->height);

   if (enabled)
      fb_enable_cursor();
}

static void fb_use_optimized_funcs_if_possible(void)
{
   if (in_hypervisor())
      framebuffer_vi.scroll_one_line_up = fb_scroll_one_line_up;

   if (in_panic())
      return;

   if (fb_get_bpp() != 32) {
      printk("[fb_console] WARNING: using slower code for bpp = %d\n",
             fb_get_bpp());
      printk("[fb_console] switch to a resolution with bpp = 32 if possible\n");
      return;
   }

   if (!fb_pre_render_char_scanlines()) {
      printk("WARNING: fb_pre_render_char_scanlines failed.\n");
      return;
   }

   use_optimized = true;

   framebuffer_vi.set_char_at = fb_set_char_at_optimized;
   framebuffer_vi.set_row = fb_set_row_optimized;
   printk("[fb_console] Use optimized functions\n");
}

void init_framebuffer_console(bool use_also_serial_port)
{
   ASSERT(use_framebuffer());
   ASSERT(fb_get_width() > 0);

   cursor_color = vga_rgb_colors[COLOR_BRIGHT_WHITE];

   psf2_header *h = fb_get_width() / 8 < 160
                        ? (void *)&_binary_font8x16_psf_start
                        : (void *)&_binary_font16x32_psf_start;

   fb_set_font(h);

   ASSERT(h->magic == PSF2_FONT_MAGIC); // Support only PSF2
   ASSERT(!(h->width % 8)); // Support only fonts with width = 8, 16, 24, 32, ..

   fb_map_in_kernel_space();
   fb_setup_banner();

   fb_term_rows = (fb_get_height() - fb_offset_y) / h->height;
   fb_term_cols = fb_get_width() / h->width;

   if (!in_panic()) {
      under_cursor_buf = kmalloc(sizeof(u32) * h->width * h->height);

      if (!under_cursor_buf)
         printk("WARNING: fb_console: unable to allocate under_cursor_buf!\n");
   }

   init_term(&framebuffer_vi,
             fb_term_rows,
             fb_term_cols,
             use_also_serial_port);

   printk("[fb_console] screen resolution: %i x %i x %i bpp\n",
          fb_get_width(), fb_get_height(), fb_get_bpp());
   printk("[fb_console] font size: %i x %i, term size: %i x %i\n",
          h->width, h->height, fb_term_cols, fb_term_rows);

   fb_use_optimized_funcs_if_possible();

   if (in_panic())
      return;

   blink_thread_ti = kthread_create(fb_blink_thread, NULL);

   if (!blink_thread_ti) {
      printk("WARNING: unable to create the fb_blink_thread\n");
   }

   if (fb_offset_y) {
      if (!kthread_create(fb_update_banner_kthread, NULL))
         printk("WARNING: unable to create the fb_update_banner_kthread\n");
   }
}

void internal_selftest_fb_perf(bool use_fpu)
{
   if (!__use_framebuffer)
      panic("Unable to test framebuffer's performance: we're in text-mode");

   const int iters = 20;
   u64 start, duration, cycles;

   start = RDTSC();

   for (int i = 0; i < iters; i++) {
      u32 color = vga_rgb_colors[i % 2 ? COLOR_WHITE : COLOR_BLACK];
      fb_raw_perf_screen_redraw(color, use_fpu);
   }

   duration = RDTSC() - start;
   cycles = duration / iters;

   u64 pixels = fb_get_width() * fb_get_height();
   printk("fb size (pixels): %u\n", pixels);
   printk("cycles per redraw: %llu\n", cycles);
   printk("cycles per 32 pixels: %llu\n", 32 * cycles / pixels);
   printk("use_fpu: %d\n", use_fpu);

   fb_draw_banner();
}

void selftest_fb_perf(void)
{
   internal_selftest_fb_perf(false);
}

void selftest_fb_perf_fpu(void)
{
   internal_selftest_fb_perf(true);
}
