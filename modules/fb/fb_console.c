/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_fb.h>
#include <tilck_gen_headers/mod_console.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/color_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/term.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/cmdline.h>

#include <tilck/mods/fb_console.h>
#include <tilck/mods/acpi.h>

#include "fb_int.h"

extern char _binary_font8x16_psf_start;
extern char _binary_font16x32_psf_start;

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

/*
 * Battery charge per mille. Valid values in range [0, 1000].
 *
 * Special values:
 *
 *    -1    No initialized
 *    -2    Read failed with an unrecoverable error
 *
 * See: fb_banner_update_battery_pm()
 */
static int batt_charge_pm = -1;

static struct video_interface framebuffer_vi;

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

static void fb_set_row_failsafe(u16 row, u16 *data, bool fpu_allowed)
{
   for (u16 i = 0; i < fb_term_cols; i++)
      fb_set_char_at_failsafe(row, i, data[i]);

   fb_reset_blink_timer();
}

static void fb_set_row_optimized(u16 row, u16 *data, bool fpu_allowed)
{
   fb_draw_row_optimized(fb_offset_y + row * font_h,
                         data,
                         fb_term_cols,
                         fpu_allowed);

   fb_reset_blink_timer();
}

void fb_draw_banner(void);

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

static struct video_interface framebuffer_vi =
{
   fb_set_char_at_failsafe,
   fb_set_row_failsafe,
   fb_clear_row,
   fb_move_cursor,
   fb_enable_cursor,
   fb_disable_cursor,
   NULL,  /* scroll_one_line_up: used only when running in a VM */
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

static void fb_banner_update_battery_pm(void)
{
   ulong charge;
   int rc;

   if (batt_charge_pm == -2)
      return; /* Read already failed with an unrecoverable error */

   if (get_acpi_init_status() != ais_fully_initialized)
      return; /* Do nothing */

   /* Do the actual read */
   rc = acpi_get_all_batteries_charge_per_mille(&charge);

   if (!rc)
      batt_charge_pm = (int)charge;
   else if (rc != -ENOMEM)
      batt_charge_pm = -2; /* Unrecoverable error (anything != out of memory) */
}

static u32 fb_banner_left_side(char *buf, size_t buf_sz)
{
   int ttynum = 1;
   int rc;

   if (get_curr_tty())
      ttynum = get_curr_tty_num();

   if (ttynum > 0) {
      rc = snprintk(buf, buf_sz, "Tilck [%s] [tty %d]", BUILDTYPE_STR, ttynum);
   } else {
      rc = snprintk(buf, buf_sz, "Tilck [%s]", BUILDTYPE_STR);
   }

   ASSERT(rc >= 0);
   return MIN((u32)rc, buf_sz);
}

static u32 fb_banner_right_side(char *buf, size_t buf_sz)
{
   struct datetime d;
   int rc;

   if (batt_charge_pm == -1) {

      /*
       * Read the current battery charge per mille ONLY IF the value has never
       * been read. The purpose of this is to avoid forcing battery's controller
       * to do too many charge reads.
       *
       * The actual value is updated peridically by fb_update_banner().
       */

      fb_banner_update_battery_pm();
   }

   timestamp_to_datetime(get_timestamp(), &d);

   if (batt_charge_pm >= 0) {

      rc = snprintk(buf, buf_sz,
                    "[Battery: %d.%d%%] [%02i %s %i %02i:%02i]",
                    batt_charge_pm / 10,
                    batt_charge_pm % 10,
                    d.day, months3[d.month - 1],
                    d.year, d.hour, d.min);

   } else {

      rc = snprintk(buf, buf_sz,
                    "[%02i %s %i %02i:%02i]",
                    d.day, months3[d.month - 1],
                    d.year, d.hour, d.min);
   }

   ASSERT(rc >= 0);
   return MIN((u32)rc, buf_sz);
}

void fb_draw_banner(void)
{
   static bool oom;
   static char *lbuf;
   static char *rbuf;

   if (oom || !fb_offset_y)
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

   u32 llen, rlen, padding = 0;
   ASSERT(fb_offset_y >= font_h);

   /*
    * Prepare banner's content.
    *
    * It's worth remarking that we're passing to our aux funcs fb_term_cols + 1
    * as length which is the real buffer's length, not `fb_term_cols`. That's
    * because snprintk() will always zero-terminate the buffer and it cannot do
    * that in the corner case where all the columns have been used, without
    * dropping one column. I.e. with a buffer of 80 chars, we can have at most
    * 79 chars of data + 1 for the NUL terminator. Therefore, our buffers are +1
    * char bigger.
    */
   llen = fb_banner_left_side(lbuf, fb_term_cols + 1);
   rlen = fb_banner_right_side(rbuf, fb_term_cols + 1 - llen);

   if (llen + rlen < fb_term_cols) {

      /* Calculate the padding */
      padding = fb_term_cols - llen - rlen;

      /* Insert the padding after the content in the left buffer */
      memset(lbuf + llen, ' ', padding);
   }

   /* Copy the whole right buffer in the left buffer, after the padding */
   memcpy(lbuf + llen + padding, rbuf, rlen);

   /* Make sure that, if we used the whole buffer, it is NUL-terminated */
   lbuf[fb_term_cols] = 0;

   /* Clear the banner area */
   fb_raw_color_lines(0, fb_offset_y, 0 /* black */);

   /* Draw the horizontal banner separation line */
   fb_raw_color_lines(fb_offset_y - 4, 1, vga_rgb_colors[COLOR_BRIGHT_WHITE]);

   /* Draw the actual banner's text */
   fb_draw_string_at_raw(0, font_h/2, lbuf, COLOR_BRIGHT_YELLOW);

   /* Clear the remaining screen area below the banner and the text */
   u32 top_lines_used = fb_offset_y + font_h * fb_term_rows;
   fb_raw_color_lines(top_lines_used,
                      fb_get_height() - top_lines_used,
                      vga_rgb_colors[COLOR_BLACK]);
}

static void fb_update_banner()
{
   while (true) {

      if (!banner_refresh_disabled) {
         fb_banner_update_battery_pm();
         fb_draw_banner();
      }

      kernel_sleep(30 * TIMER_HZ);
   }
}

static u32 fb_console_on_acpi_full_init_func(void *ctx)
{
   if (!banner_refresh_disabled)
      fb_draw_banner();

   return 0;
}

static struct acpi_reg_callback_node fb_console_on_acpi_full_init_node = {
   .node = STATIC_LIST_NODE_INIT(fb_console_on_acpi_full_init_node.node),
   .cb = &fb_console_on_acpi_full_init_func,
   .ctx = NULL
};

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
      printk("fb_console: WARNING: fb_pre_render_char_scanlines failed.\n");
      return;
   }

   disable_interrupts_forced();
   {
      use_optimized = true;
      framebuffer_vi.set_char_at = fb_set_char_at_optimized;
      framebuffer_vi.set_row = fb_set_row_optimized;
   }
   enable_interrupts_forced();
}

static void fb_use_optimized_funcs_if_possible(void)
{
   if (in_hypervisor())
      framebuffer_vi.scroll_one_line_up = fb_scroll_one_line_up;

   if (in_panic())
      return;

   if (kopt_fb_no_opt) {
      printk("fb_console: optimized funcs won't be used (kopt_fb_no_opt)\n");
      return;
   }

   if (font_w % 8) {
      printk("fb_console: WARNING: using slower code for font %d x %d\n",
             font_w, font_h);
      printk("fb_console: switch to a font having width = 8, 16, 24, ...\n");
      return;
   }

   if (fb_get_bpp() != 32) {
      printk("fb_console: WARNING: using slower code for bpp = %d\n",
             fb_get_bpp());
      printk("fb_console: switch to a resolution with bpp = 32 if possible\n");
      return;
   }

   if (kmalloc_get_max_tot_heap_free() < FBCON_OPT_FUNCS_MIN_FREE_HEAP) {
      printk("fb_console: Not using fast funcs in order to save memory\n");
      return;
   }

   if (kthread_create(async_pre_render_scanlines, 0, NULL) < 0)
      printk("fb_console: WARNING: unable to create a kthread for "
             "async_pre_render_scanlines\n");
}

bool fb_is_using_opt_funcs(void)
{
   return use_optimized;
}

static void fb_create_cursor_blinking_thread(void)
{
   int tid = kthread_create(fb_blink_thread, 0, NULL);

   if (tid < 0) {
      printk("WARNING: unable to create the fb_blink_thread\n");
      return;
   }

   disable_preemption();
   {
      blink_thread_ti = get_task(tid);
      ASSERT(blink_thread_ti != NULL);
   }
   enable_preemption();
}

void init_fb_console(void)
{
   ASSERT(use_framebuffer());
   ASSERT(fb_get_width() > 0);

   cursor_color = vga_rgb_colors[COLOR_BRIGHT_WHITE];

   void *font = fb_get_width() / 8 <= FBCON_BIGFONT_THR
                  ? (void *)&_binary_font8x16_psf_start
                  : (void *)&_binary_font16x32_psf_start;

   fb_set_font(font);
   fb_map_in_kernel_space();

   if (FB_CONSOLE_BANNER)
      fb_setup_banner();

   fb_term_rows = (fb_get_height() - fb_offset_y) / font_h;
   fb_term_cols = fb_get_width() / font_w;

   if (!in_panic()) {

      under_cursor_buf = kalloc_array_obj(u32, font_w * font_h);

      if (!under_cursor_buf)
         printk("WARNING: fb_console: unable to allocate under_cursor_buf!\n");

   } else {

      fb_term_cols = MIN(fb_term_cols, FAILSAFE_COLS);
      fb_term_rows = MIN(fb_term_rows, FAILSAFE_ROWS);
   }

   init_first_video_term(&framebuffer_vi,
                         (u16) fb_term_rows,
                         (u16) fb_term_cols,
                         -1);

   printk_flush_ringbuf();

   printk("fb_console: screen resolution: %i x %i x %i bpp\n",
          fb_get_width(), fb_get_height(), fb_get_bpp());
   printk("fb_console: font size: %i x %i, term size: %i x %i\n",
          font_w, font_h, fb_term_cols, fb_term_rows);

   fb_use_optimized_funcs_if_possible();

   if (in_panic())
      return;

   if (FB_CONSOLE_CURSOR_BLINK)
      fb_create_cursor_blinking_thread();

   if (fb_offset_y) {
      if (kthread_create(fb_update_banner, 0, NULL) > 0) {

         /* Success */
         if (MOD_acpi) {
            acpi_reg_on_full_init_cb(&fb_console_on_acpi_full_init_node);
         }

      } else {

         /* Oops! Something went wrong */
         printk("WARNING: unable to create the fb_update_banner\n");
      }
   }
}
