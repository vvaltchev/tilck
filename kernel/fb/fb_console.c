
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/arch/generic_x86/vga_textmode_defs.h>

#include <exos/fb_console.h>
#include <exos/term.h>
#include <exos/hal.h>
#include <exos/kmalloc.h>

#include "fb_int.h"

bool use_framebuffer;

static u32 fb_term_rows;
static u32 fb_term_cols;
static u32 fb_offset_y;

static bool cursor_enabled;
static int cursor_row;
static int cursor_col;
static u32 *under_cursor_buf;
static u32 cursor_color = fb_make_color(255, 255, 255);

static u32 vga_rgb_colors[16] =
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

/* video_interface */

void fb_set_char_at(int row, int col, u16 entry)
{
   psf2_header *h = (void *)&_binary_font_psf_start;

   u8 color = vgaentry_color(entry);

   fb_draw_char_raw(col * h->width,
                    fb_offset_y + row * h->height,
                    vga_rgb_colors[color & 15],
                    vga_rgb_colors[color >> 4],
                    vgaentry_char(entry));

   if (row == cursor_row && col == cursor_col)
      fb_save_under_cursor_buf();
}

static void fb_set_row(int row, u16 *data)
{
   for (u32 i = 0; i < fb_term_cols; i++)
      fb_set_char_at(row, i, data[i]);
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

// ---------------------------------------------

static const video_interface framebuffer_vi =
{
   fb_set_char_at,
   fb_set_row,
   fb_clear_row,
   fb_move_cursor,
   fb_enable_cursor,
   fb_disable_cursor
};

void init_framebuffer_console(void)
{
   fb_map_in_kernel_space();

   psf2_header *h = (void *)&_binary_font_psf_start;

   fb_term_rows = fb_get_height() / h->height;
   fb_term_cols = fb_get_width() / h->width;

   fb_offset_y = h->height;
   fb_term_rows--;

   under_cursor_buf = kmalloc(sizeof(u32) * h->width * h->height);
   VERIFY(under_cursor_buf != NULL);


   init_term(&framebuffer_vi, fb_term_rows, fb_term_cols, COLOR_WHITE);
   printk("[fb_console] rows: %i, cols: %i\n", fb_term_rows, fb_term_cols);
}
