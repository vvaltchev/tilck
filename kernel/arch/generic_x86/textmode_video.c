
#include <paging.h>
#include <string_util.h>
#include <hal.h>
#include <arch/generic_x86/textmode_video.h>

#define VIDEO_ADDR ((u16 *) KERNEL_PA_TO_VA(0xB8000))
#define VIDEO_COLS 80
#define VIDEO_ROWS 25
#define ROW_SIZE (VIDEO_COLS * 2)
#define SCREEN_SIZE (ROW_SIZE * VIDEO_ROWS)
#define BUFFER_ROWS (VIDEO_ROWS * 10)
#define EXTRA_BUFFER_ROWS (BUFFER_ROWS - VIDEO_ROWS)

STATIC_ASSERT(EXTRA_BUFFER_ROWS >= 0);

static u16 video_buffer[BUFFER_ROWS * VIDEO_COLS];
static u32 scroll;
static u32 max_scroll;

bool video_is_at_bottom(void)
{
   return scroll == max_scroll;
}

static void video_set_scroll(u32 requested_scroll)
{
   /*
    * 1. scroll cannot be > max_scroll
    * 2. scroll cannot be < max_scroll - EXTRA_BUFFER_ROWS, where
    *    EXTRA_BUFFER_ROWS = BUFFER_ROWS - VIDEO_ROWS.
    *    In other words, if for example BUFFER_ROWS is 26, and max_scroll is
    *    1000, scroll cannot be less than 1000 + 25 - 26 = 999, which means
    *    exactly 1 scroll row (EXTRA_BUFFER_ROWS == 1).
    */

   const u32 min_scroll =
      max_scroll > EXTRA_BUFFER_ROWS
         ? max_scroll - EXTRA_BUFFER_ROWS
         : 0;

   requested_scroll = MIN(MAX(requested_scroll, min_scroll), max_scroll);

   if (requested_scroll == scroll)
      return; /* nothing to do */

   scroll = requested_scroll;

   for (u32 i = 0; i < VIDEO_ROWS; i++) {
      u32 buffer_row = (scroll + i) % BUFFER_ROWS;
      memmove(VIDEO_ADDR + VIDEO_COLS * i,
              (const void *) (video_buffer + VIDEO_COLS * buffer_row),
              ROW_SIZE);
   }
}

void video_scroll_up(u32 lines)
{
   if (lines > scroll)
      video_set_scroll(0);
   else
      video_set_scroll(scroll - lines);
}

void video_scroll_down(u32 lines)
{
   video_set_scroll(scroll + lines);
}

void video_scroll_to_bottom(void)
{
   if (scroll != max_scroll) {
      video_set_scroll(max_scroll);
   }
}

void video_clear_row(int row_num)
{
   static const u16 ch_space =
      make_vgaentry(' ', make_color(COLOR_WHITE, COLOR_BLACK));

   ASSERT(0 <= row_num && row_num < VIDEO_ROWS);
   u16 *rowb = video_buffer + VIDEO_COLS * ((row_num + scroll) % BUFFER_ROWS);

   for (int i = 0; i < VIDEO_COLS; i++)
      rowb[i] = ch_space;

   u16 *row = VIDEO_ADDR + VIDEO_COLS * row_num;
   memmove(row, rowb, ROW_SIZE);
}

void video_set_char_at(char c, u8 color, int row, int col)
{
   ASSERT(0 <= row && row < VIDEO_ROWS);
   ASSERT(0 <= col && col < VIDEO_COLS);

   volatile u16 *video = (volatile u16 *)VIDEO_ADDR;
   u16 val = make_vgaentry(c, color);
   video[row * VIDEO_COLS + col] = val;
   video_buffer[(row + scroll) % BUFFER_ROWS * VIDEO_COLS + col] = val;
}

void video_add_row_and_scroll(void)
{
   max_scroll++;
   video_set_scroll(max_scroll);
   video_clear_row(VIDEO_ROWS - 1);
}

/* -------- cursor management functions ----------- */

void video_move_cursor(int row, int col)
{
   u16 position = (row * VIDEO_COLS) + col;

   // cursor LOW port to vga INDEX register
   outb(0x3D4, 0x0F);
   outb(0x3D5, (u8)(position & 0xFF));
   // cursor HIGH port to vga INDEX register
   outb(0x3D4, 0x0E);
   outb(0x3D5, (u8)((position >> 8) & 0xFF));
}

void video_enable_cursor(void)
{
   const u8 scanline_start = 0;
   const u8 scanline_end = 15;

   outb(0x3D4, 0x0A);
   outb(0x3D5, (inb(0x3D5) & 0xC0) | scanline_start);

   outb(0x3D4, 0x0B);
   outb(0x3D5, (inb(0x3E0) & 0xE0) | scanline_end);
}

void video_disable_cursor(void)
{
   outb(0x3D4, 0x0A);
   outb(0x3D5, 0x20);
}
