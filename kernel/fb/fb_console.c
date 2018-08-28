
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/color_defs.h>

#include <tilck/kernel/fb_console.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/process.h>
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
static int cursor_row;
static int cursor_col;
static u32 *under_cursor_buf;
static volatile bool cursor_visible = true;
static task_info *blink_thread_ti;
static const u32 blink_half_period = (TIMER_HZ * 45)/100;
static u32 cursor_color = fb_make_color(255, 255, 255);

/* Could we really need more than 256 rows? Probably we won't. */
static bool rows_to_flush[256];

static video_interface framebuffer_vi;

#define DARK_VAL    (168 /* vga */ + 0)
#define BRIGHT_VAL  (252 /* vga */ + 0)

u32 vga_rgb_colors[16] =
{
   [COLOR_BLACK] = fb_make_color(0, 0, 0),
   [COLOR_BLUE] = fb_make_color(0, 0, DARK_VAL + 70),
   [COLOR_GREEN] = fb_make_color(0, DARK_VAL, 0),
   [COLOR_CYAN] = fb_make_color(0, DARK_VAL, DARK_VAL),
   [COLOR_RED] = fb_make_color(DARK_VAL, 0, 0),
   [COLOR_MAGENTA] = fb_make_color(DARK_VAL, 0, DARK_VAL),
   [COLOR_YELLOW] = fb_make_color(DARK_VAL, DARK_VAL, 0),
   [COLOR_WHITE] = fb_make_color(208, 208, 208),
   [COLOR_BRIGHT_BLACK] = fb_make_color(DARK_VAL, DARK_VAL, DARK_VAL),
   [COLOR_BRIGHT_BLUE] = fb_make_color(0, 0, BRIGHT_VAL),
   [COLOR_BRIGHT_GREEN] = fb_make_color(0, BRIGHT_VAL, 0),
   [COLOR_BRIGHT_CYAN] = fb_make_color(0, BRIGHT_VAL, BRIGHT_VAL),
   [COLOR_BRIGHT_RED] = fb_make_color(BRIGHT_VAL, 0, 0),
   [COLOR_BRIGHT_MAGENTA] = fb_make_color(BRIGHT_VAL, 0, BRIGHT_VAL),
   [COLOR_BRIGHT_YELLOW] = fb_make_color(BRIGHT_VAL, BRIGHT_VAL, 0),
   [COLOR_BRIGHT_WHITE] = fb_make_color(BRIGHT_VAL, BRIGHT_VAL, BRIGHT_VAL)
};

void fb_save_under_cursor_buf(void)
{
   if (!under_cursor_buf)
      return;

   // Assumption: bbp is 32
   psf2_header *h = fb_font_header;

   const u32 ix = cursor_col * h->width;
   const u32 iy = fb_offset_y + cursor_row * h->height;
   fb_copy_from_screen(ix, iy, h->width, h->height, under_cursor_buf);
}

void fb_restore_under_cursor_buf(void)
{
   if (!under_cursor_buf)
      return;

   // Assumption: bbp is 32
   psf2_header *h = fb_font_header;

   const u32 ix = cursor_col * h->width;
   const u32 iy = fb_offset_y + cursor_row * h->height;
   fb_copy_to_screen(ix, iy, h->width, h->height, under_cursor_buf);

   rows_to_flush[cursor_row] = true;
}

static void fb_reset_blink_timer(void)
{
   if (!blink_thread_ti)
      return;

   cursor_visible = true;
   wait_obj *w = &blink_thread_ti->wobj;
   kthread_timer_sleep_obj *timer = w->ptr;

   if (timer) {
      timer->ticks_to_sleep = blink_half_period;
   }
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
   rows_to_flush[row] = true;
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
   rows_to_flush[row] = true;
}



void fb_clear_row(int row_num, u8 color)
{
   psf2_header *h = fb_font_header;
   const u32 iy = fb_offset_y + row_num * h->height;
   fb_raw_color_lines(iy, h->height, vga_rgb_colors[get_color_bg(color)]);

   if (cursor_row == row_num)
      fb_save_under_cursor_buf();

   rows_to_flush[row_num] = true;
}

void fb_move_cursor(int row, int col, int cursor_vga_color)
{
   if (!under_cursor_buf)
      return;

   psf2_header *h = fb_font_header;

   fb_restore_under_cursor_buf();

   rows_to_flush[row] = true;
   rows_to_flush[cursor_row] = true;

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
   rows_to_flush[row] = true;
}

static void fb_set_row_optimized(int row, u16 *data, bool flush)
{
   psf2_header *h = fb_font_header;

   fb_draw_char_optimized_row(fb_offset_y + row * h->height,
                              data,
                              fb_term_cols);

   if (flush) {
      fb_flush_lines(fb_offset_y + fb_font_header->height * row,
                     fb_font_header->height);
   } else {
      rows_to_flush[row] = true;
   }

   fb_reset_blink_timer();
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

   for (u32 r = 0; r < fb_term_rows; r++)
      rows_to_flush[r] = true;
}


static void fb_flush(void)
{
   bool any_update = false;

   if (!framebuffer_vi.flush_buffers)
      return;

   for (u32 r = 0; r < fb_term_rows; r++) {

      if (rows_to_flush[r]) {

         if (!any_update) {
            fpu_context_begin();
            any_update = true;
         }

         fb_flush_lines(fb_offset_y + fb_font_header->height * r,
                        fb_font_header->height);

         rows_to_flush[r] = false;
      }
   }

   if (any_update)
      fpu_context_end();
}

static void fb_full_flush(void)
{
   if (!framebuffer_vi.flush_buffers)
      return;

   fpu_context_begin();
   {
      fb_flush_lines(0, fb_get_height());
   }
   fpu_context_end();

   bzero(rows_to_flush, sizeof(rows_to_flush));
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
   fb_scroll_one_line_up,
   fb_flush
};


static void fb_blink_thread()
{
   while (true) {
      cursor_visible = !cursor_visible;
      fb_move_cursor(cursor_row, cursor_col, -1);
      fb_flush();
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
   psf2_header *h = fb_font_header;
   char lbuf[fb_term_cols + 1];
   char rbuf[fb_term_cols + 1];
   int llen, rlen, padding, i;
   datetime_t d;

   ASSERT(fb_offset_y >= h->height);

   read_system_clock_datetime(&d);

   llen = snprintk(lbuf, sizeof(lbuf) - 1 - 1,
                   "Tilck [%s build] framebuffer console", BUILDTYPE_STR);

   rlen = snprintk(rbuf, sizeof(rbuf) - 1 - llen - 1,
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
}

static void fb_flush_banner(void)
{
   fpu_context_begin();
   {
      fb_flush_lines(0, fb_offset_y);
   }
   fpu_context_end();
}

static void fb_update_banner_kthread()
{
   while (true) {
      fb_draw_banner();
      fb_flush_banner();
      kernel_sleep(60 * TIMER_HZ);
   }
}

static void fb_use_optimized_funcs_if_possible(void)
{
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
   fb_font_header = fb_get_width() / 8 < 160
                        ? (void *)&_binary_font8x16_psf_start
                        : (void *)&_binary_font16x32_psf_start;

   psf2_header *h = fb_font_header;

   ASSERT(h->magic == PSF2_FONT_MAGIC); // Support only PSF2
   ASSERT(!(h->width % 8)); // Support only fonts with width = 8, 16, 24, 32, ..

   fb_map_in_kernel_space();

   if (framebuffer_vi.flush_buffers && !in_panic() && !in_hypervisor()) {
      /*
       * In hypervisors, using double buffering just slows the fb_console,
       * therefore, we enable it only when running on bare-metal.
       */

      if (fb_alloc_shadow_buffer()) {
         printk("[fb_console] Using double buffering\n");
      } else {
         printk("WARNING: unable to use double buffering for the framebuffer\n");
      }
   }

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

void selftest_fb_perf_manual()
{
   const int iters = 20;
   u64 start, duration, cycles;

   start = RDTSC();

   for (int i = 0; i < iters; i++) {

      fb_raw_color_lines(0, fb_get_height(),
                         vga_rgb_colors[i % 2 ? COLOR_WHITE : COLOR_BLACK]);

      if (framebuffer_vi.flush_buffers)
         fb_full_flush();
   }

   duration = RDTSC() - start;
   cycles = duration / (iters);

   u64 pixels = fb_get_width() * fb_get_height();
   printk("fb size (pixels): %u\n", pixels);
   printk("cycles per redraw: %llu\n", cycles);
   printk("cycles per 32 pixels: %llu\n", 32 * cycles / pixels);

   fb_draw_banner();
   fb_flush_banner();
}
