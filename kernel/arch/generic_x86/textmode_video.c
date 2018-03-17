
#include <term.h>
#include <paging.h>
#include <string_util.h>
#include <arch/generic_x86/x86_utils.h>

#define BUFFER_ROWS 1024
//static u16 video_buffer[BUFFER_ROWS * 80];

#define VIDEO_ADDR ((volatile u16*) KERNEL_PA_TO_VA(0xB8000))
#define VIDEO_COLS 80
#define VIDEO_ROWS 25
#define ROW_SIZE (VIDEO_COLS * 2)

void video_scroll_up_one_line(void)
{
   memmove((void *) VIDEO_ADDR,
           (const void *) (VIDEO_ADDR + VIDEO_COLS),
           ROW_SIZE * (VIDEO_ROWS - 1));
}

void video_clear_row(int row_num)
{
   ASSERT(0 <= row_num && row_num < VIDEO_ROWS);
   volatile u16 *row = VIDEO_ADDR + VIDEO_COLS * row_num;
   bzero((void *)row, ROW_SIZE);
}

void video_set_char_at(char c, u8 color, int row, int col)
{
   ASSERT(0 <= row && row < VIDEO_ROWS);
   ASSERT(0 <= col && col < VIDEO_COLS);

   volatile u16 *video = VIDEO_ADDR;
   video[row * VIDEO_COLS + col] = make_vgaentry(c, color);
}

void video_movecur(int row, int col)
{
   u16 position = (row * VIDEO_COLS) + col;

   // cursor LOW port to vga INDEX register
   outb(0x3D4, 0x0F);
   outb(0x3D5, (unsigned char)(position & 0xFF));
   // cursor HIGH port to vga INDEX register
   outb(0x3D4, 0x0E);
   outb(0x3D5, (unsigned char)((position >> 8) & 0xFF));
}

