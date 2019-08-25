/* SPDX-License-Identifier: BSD-2-Clause */
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>
#include <tilck/common/color_defs.h>

#define VIDEO_ADDR ((u16*)(0xB8000))

#define TERM_COLS  80
#define TERM_ROWS  25

static u16 curr_row;
static u16 curr_col;
static u16 curr_color;

void bt_setcolor(u8 color)
{
   curr_color = color;
}

void bt_movecur(int row, int col)
{
   row = CLAMP(row, 0, TERM_ROWS - 1);
   col = CLAMP(col, 0, TERM_COLS - 1);

   u16 position = row * TERM_COLS + col;

   // cursor LOW port to vga INDEX register
   outb(0x3D4, 0x0F);
   outb(0x3D5, (u8)(position & 0xFF));
   // cursor HIGH port to vga INDEX register
   outb(0x3D4, 0x0E);
   outb(0x3D5, (u8)((position >> 8) & 0xFF));

   curr_row = (u16)row;
   curr_col = (u16)col;
}

static void bt_incr_row()
{
   if (curr_row < TERM_ROWS - 1) {
      ++curr_row;
      return;
   }

   // We have to scroll...

   memmove(VIDEO_ADDR,
           VIDEO_ADDR + TERM_COLS,
           TERM_COLS * (TERM_ROWS - 1) * 2);

   u16 *lastRow = VIDEO_ADDR + TERM_COLS * (TERM_ROWS - 1);
   memset16(lastRow, make_vgaentry(' ', curr_color), TERM_COLS);
}

void bt_write_char(char c)
{
   if (c == '\n') {
      curr_col = 0;
      bt_incr_row();
      bt_movecur(curr_row, curr_col);
      return;
   }

   if (c == '\r') {
      curr_col = 0;
      bt_movecur(curr_row, curr_col);
      return;
   }

   if (c == '\t')
      return;

   volatile u16 *video = (volatile u16 *)VIDEO_ADDR;

   const size_t offset = curr_row * TERM_COLS + curr_col;
   video[offset] = make_vgaentry(c, curr_color);
   ++curr_col;

   if (curr_col == TERM_COLS) {
      curr_col = 0;
      bt_incr_row();
   }

   bt_movecur(curr_row, curr_col);
}

void init_bt(void)
{
   /*
    * Set the current row and the current col to 0, in case the BSS variables
    * were not zero-ed because of some bug. We still need to be able to show
    * something on the screen.
    */
   curr_row = curr_col = 0;

   bt_movecur(0, 0);
   bt_setcolor(make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));
   memset16(VIDEO_ADDR, make_vgaentry(' ', curr_color), TERM_COLS * TERM_ROWS);
}
