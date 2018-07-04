#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>
#include <exos/common/arch/generic_x86/x86_utils.h>
#include <exos/common/vga_textmode_defs.h>

#define TERMINAL_VIDEO_ADDR ((u16*)(0xB8000))

#define TERM_WIDTH  80
#define TERM_HEIGHT 25

u16 terminal_row = 0;
u16 terminal_column = 0;
u16 terminal_color = 0;

void bt_setcolor(uint8_t color)
{
   terminal_color = color;
}

void bt_movecur(int row, int col)
{
   uint16_t position = (row * TERM_WIDTH) + col;

   // cursor LOW port to vga INDEX register
   outb(0x3D4, 0x0F);
   outb(0x3D5, (unsigned char)(position & 0xFF));
   // cursor HIGH port to vga INDEX register
   outb(0x3D4, 0x0E);
   outb(0x3D5, (unsigned char)((position >> 8) & 0xFF));

   terminal_row = row;
   terminal_column = col;
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

   for (int i = 0; i < TERM_WIDTH; i++) {
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

   bt_setcolor(make_color(COLOR_WHITE, COLOR_BLACK));
   volatile uint16_t *ptr = (volatile uint16_t *)TERMINAL_VIDEO_ADDR;

   for (int i = 0; i < TERM_WIDTH*TERM_HEIGHT; ++i) {
      *ptr++ = make_vgaentry(' ', terminal_color);
   }
}
