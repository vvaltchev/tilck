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
#include <tilck/kernel/tty.h>

#include "fb_int.h"

extern char _binary_font8x16_psf_start;
extern char _binary_font16x32_psf_start;

bool __use_framebuffer;

static bool use_optimized;
static u32 fb_term_rows;
static u32 fb_term_cols;
static u32 fb_offset_y;

static bool cursor_enabled;
static bool banner_refresh_disabled;
static u16 cursor_row;
static u16 cursor_col;
static u32 *under_cursor_buf;
static volatile bool cursor_visible = true;
static struct task *blink_thread_ti;
static const u32 blink_half_period = (TIMER_HZ * 45)/100;
static u32 cursor_color;

static video_interface framebuffer_vi;

static void fb_save_under_cursor_buf(void)
{
   if (!under_cursor_buf)
      return;

   if (cursor_row >= fb_term_rows || cursor_col >= fb_term_cols)
      return;

   const u32 ix = cursor_col * font_w;
   const u32 iy = fb_offset_y + cursor_row * font_h;
   fb_copy_from_screen(ix, iy, font_w, font_h, under_cursor_buf);
}

static void fb_restore_under_cursor_buf(void)
{
   if (!under_cursor_buf)
      return;

   if (cursor_row >= fb_term_rows || cursor_col >= fb_term_cols)
      return;

   const u32 ix = cursor_col * font_w;
   const u32 iy = fb_offset_y + cursor_row * font_h;
   fb_copy_to_screen(ix, iy, font_w, font_h, under_cursor_buf);
}

static void fb_reset_blink_timer(void)
{
   if (!blink_thread_ti)
      return;

   cursor_visible = true;
   task_update_wakeup_timer_if_any(blink_thread_ti, blink_half_period);
}

/* video_interface */

static void fb_set_char_at_failsafe(u16 row, u16 col, u16 entry)
{
   fb_draw_char_failsafe(col * font_w,
                         fb_offset_y + row * font_h,
                         entry);

   if (row == cursor_row && col == cursor_col)
      fb_save_under_cursor_buf();

   fb_reset_blink_timer();
}

static void fb_set_char_at_optimized(u16 row, u16 col, u16 entry)
{
   fb_draw_char_optimized(col * font_w,
                          fb_offset_y + row * font_h,
                          entry);

   if (row == cursor_row && col == cursor_col)
      fb_save_under_cursor_buf();

   fb_reset_blink_timer();
}

static void fb_clear_row(u16 row_num, u8 color)
{
   const u32 iy = fb_offset_y + row_num * font_h;
   fb_raw_color_lines(iy, font_h, vga_rgb_colors[get_color_bg(color)]);

   if (cursor_row == row_num)
      fb_save_under_cursor_buf();
}

static void fb_move_cursor(u16 row, u16 col, int cursor_vga_color)
{
   if (!under_cursor_buf)
      return;

   fb_restore_under_cursor_buf();

   if (row != cursor_row || col != cursor_col)
      cursor_visible = true;

   cursor_row = row;
   cursor_col = col;

   if (cursor_vga_color >= 0)
      cursor_color = vga_rgb_colors[cursor_vga_color];

   if (!cursor_enabled)
      return;

   fb_save_under_cursor_buf();

   if (cursor_visible) {

      if (cursor_row < fb_term_rows && cursor_col < fb_term_cols) {
         fb_draw_cursor_raw(cursor_col * font_w,
                            fb_offset_y + cursor_row * font_h,
                            cursor_color);
      }

      fb_reset_blink_timer();
   }
}

static void fb_enable_cursor(void)
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

static void fb_disable_cursor(void)
{
   if (!cursor_enabled)
      return;

   cursor_enabled = false;
   fb_move_cursor(cursor_row, cursor_col, -1);
}

static void fb_set_row_failsafe(u16 row, u16 *data, bool flush)
{
   for (u16 i = 0; i < fb_term_cols; i++)
      fb_set_char_at_failsafe(row, i, data[i]);

   fb_reset_blink_timer();
}

static void fb_set_row_optimized(u16 row, u16 *data, bool flush)
{
   fb_draw_char_optimized_row(fb_offset_y + row * font_h,
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
   fb_enable_banner_refresh,
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
   if (use_optimized)

      for (; *str; str++, x += font_w)
         fb_draw_char_optimized(x, y, make_vgaentry(*str, color));
   else

      for (; *str; str++, x += font_w)
         fb_draw_char_failsafe(x, y, make_vgaentry(*str, color));
}

static void fb_setup_banner(void)
{
   if (in_panic())
      return;

   fb_offset_y = (20 * font_h) / 10;
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

   u32 llen, rlen, padding, i;
   struct datetime d;
   int rc, ttynum = 1;

   ASSERT(fb_offset_y >= font_h);

   read_system_clock_datetime(&d);

   if (get_curr_tty())
      ttynum = get_curr_tty_num();

   if (ttynum > 0) {
      rc = snprintk(lbuf, fb_term_cols - 1,
                    "Tilck [%s] fb console [tty %d]",
                    BUILDTYPE_STR, ttynum);
   } else {
      rc = snprintk(lbuf, fb_term_cols - 1,
                    "Tilck [%s] fb console [tty: kernel debug panel]",
                    BUILDTYPE_STR);
   }

   ASSERT(rc > 0);
   llen = MIN((u16)rc, fb_term_cols - 1);

   rc = snprintk(rbuf, fb_term_cols - 1 - llen,
                 "%02i %s %i %02i:%02i",
                 d.day, months3[d.month - 1],
                 d.year, d.hour, d.min);

   ASSERT(rc > 0);
   rlen = MIN((u32)rc, fb_term_cols - 1 - llen);

   padding = fb_term_cols - 1 - llen - rlen;

   for (i = llen; i < llen + padding; i++)
      lbuf[i] = ' ';

   memcpy(lbuf + i, rbuf, rlen);
   lbuf[fb_term_cols - 1] = 0;

   fb_raw_color_lines(0, fb_offset_y, 0 /* black */);
   fb_raw_color_lines(fb_offset_y - 4, 1, vga_rgb_colors[COLOR_BRIGHT_WHITE]);
   fb_draw_string_at_raw(font_w/2, font_h/2, lbuf, COLOR_BRIGHT_YELLOW);

   u32 top_lines_used = fb_offset_y + font_h * fb_term_rows;

   fb_raw_color_lines(top_lines_used,
                      fb_get_height() - top_lines_used,
                      vga_rgb_colors[COLOR_BLACK]);
}

static void fb_update_banner()
{
   while (true) {

      if (!banner_refresh_disabled)
         fb_draw_banner();

      kernel_sleep(60 * TIMER_HZ);
   }
}

static void fb_scroll_one_line_up(void)
{
   bool enabled = cursor_enabled;

   if (enabled)
     fb_disable_cursor();

   fb_lines_shift_up(fb_offset_y + font_h,  /* source: row 1 (+ following) */
                     fb_offset_y,           /* destination: row 0 */
                     fb_get_height() - fb_offset_y - font_h);

   if (enabled)
      fb_enable_cursor();
}

static void async_pre_render_scanlines()
{
   if (!fb_pre_render_char_scanlines()) {
      printk("[fb_console] WARNING: fb_pre_render_char_scanlines failed.\n");
      return;
   }

   uptr var;
   disable_interrupts(&var);
   {
      use_optimized = true;
      framebuffer_vi.set_char_at = fb_set_char_at_optimized;
      framebuffer_vi.set_row = fb_set_row_optimized;
   }
   enable_interrupts(&var);
}

static void fb_use_optimized_funcs_if_possible(void)
{
   if (in_hypervisor())
      framebuffer_vi.scroll_one_line_up = fb_scroll_one_line_up;

   if (in_panic())
      return;

   if (font_w % 8) {
      printk("[fb_console] WARNING: using slower code for font %d x %d\n",
             font_w, font_h);
      printk("[fb_console] switch to a font having width = 8, 16, 24, ...\n");
      return;
   }

   if (fb_get_bpp() != 32) {
      printk("[fb_console] WARNING: using slower code for bpp = %d\n",
             fb_get_bpp());
      printk("[fb_console] switch to a resolution with bpp = 32 if possible\n");
      return;
   }

   if (kmalloc_get_max_tot_heap_free() < FBCON_OPT_FUNCS_MIN_FREE_HEAP) {
      printk("[fb_console] Not using fast funcs in order to save memory\n");
      return;
   }

   if (kthread_create(async_pre_render_scanlines, NULL) < 0)
      printk("[fb_console] WARNING: unable to create a kthread for "
             "async_pre_render_scanlines\n");
}

bool fb_is_using_opt_funcs(void)
{
   return use_optimized;
}

void init_framebuffer_console(void)
{
   ASSERT(use_framebuffer());
   ASSERT(fb_get_width() > 0);

   cursor_color = vga_rgb_colors[COLOR_BRIGHT_WHITE];

   void *font = fb_get_width() / 8 < 160
                  ? (void *)&_binary_font8x16_psf_start
                  : (void *)&_binary_font16x32_psf_start;

   fb_set_font(font);
   fb_map_in_kernel_space();
   fb_setup_banner();

   fb_term_rows = (fb_get_height() - fb_offset_y) / font_h;
   fb_term_cols = fb_get_width() / font_w;

   if (!in_panic()) {
      under_cursor_buf = kmalloc(sizeof(u32) * font_w * font_h);

      if (!under_cursor_buf)
         printk("WARNING: fb_console: unable to allocate under_cursor_buf!\n");
   }

   init_term(get_curr_term(),
             &framebuffer_vi,
             (u16) fb_term_rows,
             (u16) fb_term_cols,
             0,
             -1);

   printk_flush_ringbuf();

   printk("[fb_console] screen resolution: %i x %i x %i bpp\n",
          fb_get_width(), fb_get_height(), fb_get_bpp());
   printk("[fb_console] font size: %i x %i, term size: %i x %i\n",
          font_w, font_h, fb_term_cols, fb_term_rows);

   fb_use_optimized_funcs_if_possible();

   if (in_panic())
      return;

   int tid = kthread_create(fb_blink_thread, NULL);

   if (tid > 0) {
      disable_preemption();
      {
         blink_thread_ti = get_task(tid);
         ASSERT(blink_thread_ti != NULL);
      }
      enable_preemption();
   } else {
      printk("WARNING: unable to create the fb_blink_thread\n");
   }

   if (fb_offset_y) {
      if (kthread_create(fb_update_banner, NULL) < 0)
         printk("WARNING: unable to create the fb_update_banner\n");
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
