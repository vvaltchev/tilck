/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/color_defs.h>

#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/term.h>

#define VIDEO_ADDR ((u16 *) PA_TO_LIN_VA(0xB8000))
#define VIDEO_COLS 80
#define VIDEO_ROWS 25

static void textmode_clear_row(u16 row_num, u8 color)
{
   ASSERT(row_num < VIDEO_ROWS);

   memset16(VIDEO_ADDR + VIDEO_COLS * row_num,
            make_vgaentry(' ', color),
            VIDEO_COLS);
}

static void textmode_set_char_at(u16 row, u16 col, u16 entry)
{
   ASSERT(row < VIDEO_ROWS);
   ASSERT(col < VIDEO_COLS);

   volatile u16 *video = (volatile u16 *)VIDEO_ADDR;
   video[row * VIDEO_COLS + col] = entry;
}

static void textmode_set_row(u16 row, u16 *data, bool fpu_allowed)
{
   ASSERT(row < VIDEO_ROWS);

   void *dest_addr = VIDEO_ADDR + row * VIDEO_COLS;
   void *src_addr = data;

   memcpy32(dest_addr, src_addr, VIDEO_COLS >> 1);
}

/*
 * This function works, but in practice is 2x slower than just using term's
 * generic scroll and re-draw the whole screen.
 */
static void textmode_scroll_one_line_up(void)
{
   memcpy32(VIDEO_ADDR,
            VIDEO_ADDR + VIDEO_COLS,
            ((VIDEO_ROWS - 1) * VIDEO_COLS) >> 1);
}

/*
 * -------- cursor management functions -----------
 *
 * Here: http://www.osdever.net/FreeVGA/vga/textcur.htm
 * There is a lot of precious information about how to work with the cursor.
 */

static void textmode_move_cursor(u16 row, u16 col, int color /* ignored */)
{
   u16 position = (row * VIDEO_COLS) + col;

   // cursor LOW port to vga INDEX register
   outb(0x3D4, 0x0F);
   outb(0x3D5, LO_BITS(position, 8, u8));
   // cursor HIGH port to vga INDEX register
   outb(0x3D4, 0x0E);
   outb(0x3D5, LO_BITS(position >> 8, 8, u8));
}

static void textmode_enable_cursor(void)
{
   const u8 s_start = 0; /* scanline start */
   const u8 s_end = 15;  /* scanline end */

   outb(0x3D4, 0x0A);
   outb(0x3D5, (inb(0x3D5) & 0xC0) | s_start);  // Note: mask with 0xC0
                                                // which keeps only the
                                                // higher 2 bits in order
                                                // to set bit 5 to 0.

   outb(0x3D4, 0x0B);
   outb(0x3D5, (inb(0x3D5) & 0xE0) | s_end);    // Mask with 0xE0 keeps
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
   textmode_move_cursor(VIDEO_ROWS, VIDEO_COLS, 0);

   // outb(0x3D4, 0x0A);
   // outb(0x3D5, inb(0x3D5) | 0x20);
}

static const struct video_interface ega_text_mode_i =
{
   textmode_set_char_at,
   textmode_set_row,
   textmode_clear_row,
   textmode_move_cursor,
   textmode_enable_cursor,
   textmode_disable_cursor,
   NULL, /* textmode_scroll_one_line_up (see the comment) */
   NULL, /* redraw_static_elements */
   NULL, /* disable_static_elems_refresh */
   NULL, /* enable_static_elems_refresh */
};

void init_textmode_console(void)
{
   pdir_t *pdir = get_curr_pdir();

   if (pdir != NULL && !is_mapped(pdir, VIDEO_ADDR)) {
      int rc = map_page(pdir,
                        VIDEO_ADDR,
                        LIN_VA_TO_PA(VIDEO_ADDR),
                        PAGING_FL_RW);

      if (rc < 0)
         panic("textmode_console: unable to map VIDEO_ADDR in the virt space");
   }

   init_first_video_term(&ega_text_mode_i, VIDEO_ROWS, VIDEO_COLS, -1);
}
