/* SPDX-License-Identifier: BSD-2-Clause */
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>
#include <tilck/common/color_defs.h>

#define TERMINAL_VIDEO_ADDR ((u16*)(0xB8000))

#define TERM_WIDTH  80u
#define TERM_HEIGHT 25u

u16 terminal_row = 0;
u16 terminal_column = 0;
u16 terminal_color = 0;

void bt_setcolor(uint8_t color)
{
   terminal_color = color;
}

void bt_movecur(int row, int col)
{
   u16 position = (uint16_t)(
      ((uint16_t)row * TERM_WIDTH) + (uint16_t)col
   );

   // cursor LOW port to vga INDEX register
   outb(0x3D4, 0x0F);
   outb(0x3D5, (uint8_t)(position & 0xFF));
   // cursor HIGH port to vga INDEX register
   outb(0x3D4, 0x0E);
   outb(0x3D5, (uint8_t)((position >> 8) & 0xFF));

   terminal_row = (u16)row;
   terminal_column = (u16)col;
}


static void bt_incr_row()
{
   if (terminal_row < TERM_HEIGHT - 1) {
      ++terminal_row;
      return;
   }

   // We have to scroll...

   memmove(TERMINAL_VIDEO_ADDR,
           TERMINAL_VIDEO_ADDR + TERM_WIDTH,
           TERM_WIDTH * (TERM_HEIGHT - 1) * 2);

   volatile uint16_t *lastRow =
      (volatile uint16_t *)TERMINAL_VIDEO_ADDR + TERM_WIDTH * (TERM_HEIGHT - 1);

   for (u32 i = 0; i < TERM_WIDTH; i++) {
      lastRow[i] = make_vgaentry(' ', terminal_color);
   }
}

void bt_write_char(char c)
{
   if (c == '\n') {
      terminal_column = 0;
      bt_incr_row();
      bt_movecur(terminal_row, terminal_column);
      return;
   }

   if (c == '\r') {
      terminal_column = 0;
      bt_movecur(terminal_row, terminal_column);
      return;
   }

   if (c == '\t') {
      return;
   }

   volatile uint16_t *video = (volatile uint16_t *)TERMINAL_VIDEO_ADDR;

   const size_t offset = terminal_row * TERM_WIDTH + terminal_column;
   video[offset] = make_vgaentry(c, terminal_color);
   ++terminal_column;

   if (terminal_column == TERM_WIDTH) {
      terminal_column = 0;
      bt_incr_row();
   }

   bt_movecur(terminal_row, terminal_column);
}

void init_bt(void)
{
   bt_movecur(0, 0);

   bt_setcolor(make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR));
   volatile uint16_t *ptr = (volatile uint16_t *)TERMINAL_VIDEO_ADDR;

   for (u32 i = 0; i < TERM_WIDTH*TERM_HEIGHT; ++i) {
      *ptr++ = make_vgaentry(' ', terminal_color);
   }
}
