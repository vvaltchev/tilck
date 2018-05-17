
#include <common/string_util.h>
#include <common/vga_textmode_defs.h>

#include <exos/arch/generic_x86/textmode_video.h>
#include <exos/paging.h>
#include <exos/hal.h>
#include <exos/term.h>

#define VIDEO_ADDR ((u16 *) KERNEL_PA_TO_VA(0xB8000))
#define VIDEO_COLS 80
#define VIDEO_ROWS 25

static void textmode_clear_row(int row_num, u8 color)
{
   ASSERT(0 <= row_num && row_num < VIDEO_ROWS);

   memset16(VIDEO_ADDR + VIDEO_COLS * row_num,
            make_vgaentry(' ', color),
            VIDEO_COLS);
}

static void textmode_set_char_at(int row, int col, u16 entry)
{
   ASSERT(0 <= row && row < VIDEO_ROWS);
   ASSERT(0 <= col && col < VIDEO_COLS);

   volatile u16 *video = (volatile u16 *)VIDEO_ADDR;
   video[row * VIDEO_COLS + col] = entry;
}

static void textmode_set_row(int row, u16 *data)
{
   memcpy((u16 *)VIDEO_ADDR + row * VIDEO_COLS, data, VIDEO_COLS * 2);
}

/*
 * -------- cursor management functions -----------
 *
 * Here: http://www.osdever.net/FreeVGA/vga/textcur.htm
 * There is a lot of precious information about how to work with the cursor.
 */

static void textmode_move_cursor(int row, int col)
{
   u16 position = (row * VIDEO_COLS) + col;

   // cursor LOW port to vga INDEX register
   outb(0x3D4, 0x0F);
   outb(0x3D5, (u8)(position & 0xFF));
   // cursor HIGH port to vga INDEX register
   outb(0x3D4, 0x0E);
   outb(0x3D5, (u8)((position >> 8) & 0xFF));
}

static void textmode_enable_cursor(void)
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

static void textmode_disable_cursor(void)
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

static const video_interface ega_text_mode_i =
{
   textmode_set_char_at,
   textmode_set_row,
   textmode_clear_row,
   textmode_move_cursor,
   textmode_enable_cursor,
   textmode_disable_cursor,
   NULL /* scroll_one_line_up */
};

void init_textmode_console(void)
{
   init_term(&ega_text_mode_i,
             VIDEO_ROWS,
             VIDEO_COLS,
             make_color(COLOR_WHITE, COLOR_BLACK));
}
