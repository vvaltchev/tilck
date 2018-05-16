
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/arch/generic_x86/vga_textmode_defs.h>

#include <exos/fb_console.h>
#include <exos/term.h>
#include <exos/hal.h>
#include <exos/kmalloc.h>
#include <exos/process.h>
#include <exos/timer.h>
#include <exos/datetime.h>

#include "fb_int.h"

bool __use_framebuffer;

static u32 fb_term_rows;
static u32 fb_term_cols;
static u32 fb_offset_y;

static bool cursor_enabled;
static int cursor_row;
static int cursor_col;
static u32 *under_cursor_buf;
static volatile bool cursor_visible = true;
static task_info *blink_thread_ti;
static const u32 blink_half_period = (TIMER_HZ * 60)/100;
static u32 cursor_color = fb_make_color(255, 255, 255);

static video_interface framebuffer_vi;

u32 vga_rgb_colors[16] =
{
   [COLOR_BLACK] = fb_make_color(0, 0, 0),
   [COLOR_BLUE] = fb_make_color(0, 0, 168),
   [COLOR_GREEN] = fb_make_color(0, 168, 0),
   [COLOR_CYAN] = fb_make_color(0, 168, 168),
   [COLOR_RED] = fb_make_color(168, 0, 0),
   [COLOR_MAGENTA] = fb_make_color(168, 0, 168),
   [COLOR_BROWN] = fb_make_color(168, 168, 0),
   [COLOR_LIGHT_GREY] = fb_make_color(208, 208, 208),
   [COLOR_DARK_GREY] = fb_make_color(168, 168, 168),
   [COLOR_LIGHT_BLUE] = fb_make_color(0, 0, 252),
   [COLOR_LIGHT_GREEN] = fb_make_color(0, 252, 0),
   [COLOR_LIGHT_CYAN] = fb_make_color(0, 252, 252),
   [COLOR_LIGHT_RED] = fb_make_color(252, 0, 0),
   [COLOR_LIGHT_MAGENTA] = fb_make_color(252, 0, 252),
   [COLOR_LIGHT_BROWN] = fb_make_color(252, 252, 0),
   [COLOR_WHITE] = fb_make_color(252, 252, 252)
};

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
}

void fb_save_under_cursor_buf(void)
{
   // Assumption: bbp is 32
   psf2_header *h = (void *)&_binary_font_psf_start;

   const u32 ix = cursor_col * h->width;
   const u32 iy = fb_offset_y + cursor_row * h->height;
   fb_copy_from_screen(ix, iy, h->width, h->height, under_cursor_buf);
}

void fb_restore_under_cursor_buf(void)
{
   // Assumption: bbp is 32
   psf2_header *h = (void *)&_binary_font_psf_start;

   const u32 ix = cursor_col * h->width;
   const u32 iy = fb_offset_y + cursor_row * h->height;
   fb_copy_to_screen(ix, iy, h->width, h->height, under_cursor_buf);
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

void fb_set_char_at_generic(int row, int col, u16 entry)
{
   psf2_header *h = (void *)&_binary_font_psf_start;

   fb_draw_char_raw(col * h->width,
                    fb_offset_y + row * h->height,
                    entry);

   if (row == cursor_row && col == cursor_col)
      fb_save_under_cursor_buf();

   fb_reset_blink_timer();
}

void fb_set_char8x16_at(int row, int col, u16 entry)
{
   psf2_header *h = (void *)&_binary_font_psf_start;

   fb_draw_char8x16(col << 3,
                    fb_offset_y + row * h->height,
                    entry);

   if (row == cursor_row && col == cursor_col)
      fb_save_under_cursor_buf();

   fb_reset_blink_timer();
}

void fb_clear_row(int row_num, u8 color)
{
   psf2_header *h = (void *)&_binary_font_psf_start;
   const u32 iy = fb_offset_y + row_num * h->height;
   fb_raw_color_lines(iy, h->height, vga_rgb_colors[color >> 4]);
}

void fb_move_cursor(int row, int col)
{
   psf2_header *h = (void *)&_binary_font_psf_start;

   fb_restore_under_cursor_buf();

   cursor_row = row;
   cursor_col = col;

   if (cursor_enabled) {

      fb_save_under_cursor_buf();

      if (cursor_visible)
         fb_draw_cursor_raw(cursor_col * h->width,
                            fb_offset_y + cursor_row * h->height,
                            cursor_color);
   }
}

void fb_enable_cursor(void)
{
   cursor_enabled = true;
   fb_move_cursor(cursor_row, cursor_col);
}

void fb_disable_cursor(void)
{
   cursor_enabled = false;
   fb_move_cursor(cursor_row, cursor_col);
}

static void fb_set_row_generic(int row, u16 *data)
{
   for (u32 i = 0; i < fb_term_cols; i++)
      fb_set_char_at_generic(row, i, data[i]);

   fb_reset_blink_timer();
}

static void fb_set_row_char8x16(int row, u16 *data)
{
   fb_draw_char8x16_row(fb_offset_y + (row << 4),
                        data,
                        fb_term_cols);

   fb_reset_blink_timer();
}

// ---------------------------------------------

static video_interface framebuffer_vi =
{
   fb_set_char_at_generic,
   fb_set_row_generic,
   fb_clear_row,
   fb_move_cursor,
   fb_enable_cursor,
   fb_disable_cursor
};


static void fb_blink_thread()
{
   while (true) {
      cursor_visible = !cursor_visible;
      fb_move_cursor(cursor_row, cursor_col);
      kernel_sleep(blink_half_period);
   }
}

static void fb_draw_string_at_raw(u32 x, u32 y, const char *str, u8 color)
{
   psf2_header *h = (void *)&_binary_font_psf_start;

   if (framebuffer_vi.set_char_at == fb_set_char8x16_at) {

      while (*str) {
         fb_draw_char8x16(x, y, make_vgaentry(*str++, color));
         x += h->width;
      }

   } else {

      while (*str) {
         fb_draw_char_raw(x, y, make_vgaentry(*str++, color));
         x += h->width;
      }

   }
}

static void fb_setup_banner(void)
{
   psf2_header *h = (void *)&_binary_font_psf_start;

   fb_offset_y = (20 * h->height)/10;
   fb_raw_color_lines(0, fb_offset_y, 0 /* black */);
   fb_raw_color_lines(fb_offset_y - 4, 1, vga_rgb_colors[COLOR_WHITE]);
}

static void fb_draw_banner(void)
{
   psf2_header *h = (void *)&_binary_font_psf_start;
   char lbuf[fb_term_cols + 1];
   char rbuf[fb_term_cols + 1];
   int llen, rlen, padding, i;
   datetime_t d;

   if (!fb_offset_y)
      return;

   read_system_clock_datetime(&d);

   llen = snprintk(lbuf, sizeof(lbuf) - 1 - 1,
                   "exOS [%s build] framebuffer console", BUILDTYPE_STR);

   rlen = snprintk(rbuf, sizeof(rbuf) - 1 - llen - 1,
                   "%s%i/%s%i/%i %s%i:%s%i",
                   d.day < 10 ? "0" : "",
                   d.day,
                   d.month < 10 ? "0" : "",
                   d.month,
                   d.year,
                   d.hour < 10 ? "0" : "",
                   d.hour,
                   d.min < 10 ? "0" : "",
                   d.min);

   padding = (fb_term_cols - llen - rlen - 1);

   for (i = llen; i < llen + padding; i++)
      lbuf[i] = ' ';

   memcpy(lbuf + i, rbuf, rlen);
   lbuf[fb_term_cols - 1] = 0;

   fb_draw_string_at_raw(h->width/2, 7, lbuf, COLOR_LIGHT_BROWN);
}

static void fb_update_banner_kthread()
{
   while (true) {
      fb_draw_banner();
      kernel_sleep(60 * TIMER_HZ);
   }
}

void init_framebuffer_console(void)
{
   psf2_header *h = (void *)&_binary_font_psf_start;

   fb_map_in_kernel_space();

   fb_setup_banner();

   fb_term_rows = (fb_get_height() - fb_offset_y) / h->height;
   fb_term_cols = fb_get_width() / h->width;

   under_cursor_buf = kmalloc(sizeof(u32) * h->width * h->height);
   VERIFY(under_cursor_buf != NULL);

   init_term(&framebuffer_vi, fb_term_rows, fb_term_cols, COLOR_WHITE);
   printk("[fb_console] rows: %i, cols: %i\n", fb_term_rows, fb_term_cols);

   if (h->width == 8 && h->height == 16) {
      if (fb_precompute_fb_w8_char_scanlines()) {
         framebuffer_vi.set_char_at = fb_set_char8x16_at;
         framebuffer_vi.set_row = fb_set_row_char8x16;
         printk("[fb_console] Use code optimized for 8x16 fonts\n");
      } else {
         printk("WARNING: fb_precompute_fb_w8_char_scanlines failed.\n");
      }
   }

   fb_draw_banner();
}

void post_sched_init_framebuffer_console(void)
{
   if (!use_framebuffer())
      return;

   blink_thread_ti = kthread_create(fb_blink_thread, NULL);

   if (!blink_thread_ti) {
      printk("WARNING: unable to create the fb_blink_thread\n");
   }

   if (fb_offset_y) {
      kthread_create(fb_update_banner_kthread, NULL);
   }
}
