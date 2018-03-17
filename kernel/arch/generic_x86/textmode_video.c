
#include <term.h>
#include <paging.h>
#include <string_util.h>
#include <arch/generic_x86/x86_utils.h>

#define VIDEO_ADDR ((volatile u16*) KERNEL_PA_TO_VA(0xB8000))
#define VIDEO_COLS 80
#define VIDEO_ROWS 25
#define ROW_SIZE (VIDEO_COLS * 2)
#define SCREEN_SIZE (ROW_SIZE * VIDEO_ROWS)
#define BUFFER_ROWS 1024

static u16 video_buffer[BUFFER_ROWS * 80];
static int scroll = 0;
static int max_scroll = 0;

int video_get_scroll(void)
{
   return scroll;
}

int video_get_max_scroll(void)
{
   return max_scroll;
}

void video_set_scroll(int s)
{
   s = MIN(MAX(s, 0), max_scroll);

   memmove((void *) VIDEO_ADDR,
           (const void *) (video_buffer + s * VIDEO_COLS),
           SCREEN_SIZE);

   scroll = s;
}

void video_scroll_to_bottom(void)
{
   if (scroll != max_scroll) {
      video_set_scroll(max_scroll);
   }
}

void video_clear_row(int row_num)
{
   ASSERT(0 <= row_num && row_num < VIDEO_ROWS);

   volatile u16 *row = VIDEO_ADDR + VIDEO_COLS * row_num;
   bzero((void *)row, ROW_SIZE);

   u16 *buf_row = video_buffer + VIDEO_COLS * (row_num + scroll);
   bzero(buf_row, ROW_SIZE);
}

void video_set_char_at(char c, u8 color, int row, int col)
{
   ASSERT(0 <= row && row < VIDEO_ROWS);
   ASSERT(0 <= col && col < VIDEO_COLS);

   volatile u16 *video = VIDEO_ADDR;
   u16 val = make_vgaentry(c, color);
   video[row * VIDEO_COLS + col] = val;
   video_buffer[(row + scroll) * VIDEO_COLS + col] = val;
}


void video_add_row_and_scroll(void)
{
   max_scroll++;
   video_set_scroll(max_scroll);
   video_clear_row(VIDEO_ROWS - 1);
}


void video_movecur(int row, int col)
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
   uint8_t cursor_start=0;
   uint8_t cursor_end=0;

	outb(0x3D4, 0x0A);
	outb(0x3D5, (inb(0x3D5) & 0xC0) | cursor_start);

	outb(0x3D4, 0x0B);
	outb(0x3D5, (inb(0x3E0) & 0xE0) | cursor_end);
}

void video_disable_cursor(void)
{
	outb(0x3D4, 0x0A);
	outb(0x3D5, 0x20);
}
