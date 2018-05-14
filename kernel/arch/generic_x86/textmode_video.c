
#include <common/string_util.h>

#include <exos/arch/generic_x86/textmode_video.h>
#include <exos/paging.h>
#include <exos/hal.h>
#include <exos/term.h>

#define VIDEO_ADDR ((u16 *) KERNEL_PA_TO_VA(0xB8000))
#define VIDEO_COLS 80
#define VIDEO_ROWS 25
#define ROW_SIZE (VIDEO_COLS * 2)
#define SCREEN_SIZE (ROW_SIZE * VIDEO_ROWS)
#define BUFFER_ROWS (VIDEO_ROWS * 10)
#define EXTRA_BUFFER_ROWS (BUFFER_ROWS - VIDEO_ROWS)

STATIC_ASSERT(EXTRA_BUFFER_ROWS >= 0);

static u16 textmode_buffer[BUFFER_ROWS * VIDEO_COLS];
static u32 scroll;
static u32 max_scroll;

static const video_interface ega_text_mode_i =
{
   textmode_set_char_at,
   textmode_clear_row,
   textmode_scroll_up,
   textmode_scroll_down,
   textmode_is_at_bottom,
   textmode_scroll_to_bottom,
   textmode_add_row_and_scroll,
   textmode_move_cursor,
   textmode_enable_cursor,
   textmode_disable_cursor
};

void init_textmode_console(void)
{
   init_term(&ega_text_mode_i,
             VIDEO_ROWS,
             VIDEO_COLS,
             make_color(COLOR_WHITE, COLOR_BLACK));
}

bool textmode_is_at_bottom(void)
{
   return scroll == max_scroll;
}

static void textmode_set_scroll(u32 requested_scroll)
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
              (const void *) (textmode_buffer + VIDEO_COLS * buffer_row),
              ROW_SIZE);
   }
}

void textmode_scroll_up(u32 lines)
{
   if (lines > scroll)
      textmode_set_scroll(0);
   else
      textmode_set_scroll(scroll - lines);
}

void textmode_scroll_down(u32 lines)
{
   textmode_set_scroll(scroll + lines);
}

void textmode_scroll_to_bottom(void)
{
   if (scroll != max_scroll) {
      textmode_set_scroll(max_scroll);
   }
}

void textmode_clear_row(int row_num)
{
   static const u16 ch_space =
      make_vgaentry(' ', make_color(COLOR_WHITE, COLOR_BLACK));

   ASSERT(0 <= row_num && row_num < VIDEO_ROWS);
   u16 *rowb = textmode_buffer + VIDEO_COLS * ((row_num + scroll)%BUFFER_ROWS);

   memset16(rowb, ch_space, VIDEO_COLS);
   memcpy(VIDEO_ADDR + VIDEO_COLS * row_num, rowb, ROW_SIZE);
}

void textmode_set_char_at(char c, u8 color, int row, int col)
{
   ASSERT(0 <= row && row < VIDEO_ROWS);
   ASSERT(0 <= col && col < VIDEO_COLS);

   volatile u16 *video = (volatile u16 *)VIDEO_ADDR;
   u16 val = make_vgaentry(c, color);
   video[row * VIDEO_COLS + col] = val;
   textmode_buffer[(row + scroll) % BUFFER_ROWS * VIDEO_COLS + col] = val;
}

void textmode_add_row_and_scroll(void)
{
   max_scroll++;
   textmode_set_scroll(max_scroll);
   textmode_clear_row(VIDEO_ROWS - 1);
}


/*
 * -------- cursor management functions -----------
 *
 * Here: http://www.osdever.net/FreeVGA/vga/textcur.htm
 * There is a lot of precious information about how to work with the cursor.
 */

void textmode_move_cursor(int row, int col)
{
   u16 position = (row * VIDEO_COLS) + col;

   // cursor LOW port to vga INDEX register
   outb(0x3D4, 0x0F);
   outb(0x3D5, (u8)(position & 0xFF));
   // cursor HIGH port to vga INDEX register
   outb(0x3D4, 0x0E);
   outb(0x3D5, (u8)((position >> 8) & 0xFF));
}

void textmode_enable_cursor(void)
{
   const u8 scanline_start = 0;
   const u8 scanline_end = 15;

   outb(0x3D4, 0x0A);
   outb(0x3D5, (inb(0x3D5) & 0xC0) | scanline_start); // Note: mask with 0xC0
                                                      // which keeps only the
                                                      // higher 2 bits in order
                                                      // to set bit 5 to 0.

   outb(0x3D4, 0x0B);
   outb(0x3D5, (inb(0x3D5) & 0xE0) | scanline_end);   // Mask with 0xE0 keeps
                                                      // the higher 3 bits.
}

void textmode_disable_cursor(void)
{
   /*
    * Move the cursor off-screen. Yes, it seems an ugly way to do that, but it
    * seems to be the most compatible way to "disable" the cursor.
    * As claimed here: http://www.osdever.net/FreeVGA/vga/textcur.htm#enable
    * the "official" method below (commented) does not work on some hardware.
    * On my Hannspree SN10E1, I can confirm that the code below causes strange
    * effects: the cursor is offset-ed 3 chars at the right of the position
    * it should be.
    */
   textmode_move_cursor(VIDEO_ROWS, VIDEO_COLS);

   // outb(0x3D4, 0x0A);
   // outb(0x3D5, inb(0x3D5) | 0x20);
}
