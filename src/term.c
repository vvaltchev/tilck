
#include <term.h>
#include <stringUtil.h>

#define TERMINAL_VIDEO_ADDR ((volatile uint16_t*)(KERNEL_BASE_VADDR + 0xB8000))
#define TERMINAL_BUFFER_ADDR ((volatile uint16_t*)(KERNEL_BASE_VADDR + 0x10000))

#define TERMINAL_BUFFER_ROWS 1024
#define TERMINAL_SCREEN_SIZE (term_width * term_height * 2)

static int8_t term_width = 80;
static int8_t term_height = 25;

volatile uint8_t terminal_row = 0;
volatile uint8_t terminal_column = 0;
volatile uint8_t terminal_color;

volatile int buf_next_slot = 0;
volatile int scroll_value = 0;
volatile bool buf_full = false;

int term_get_scroll_value()
{
   return scroll_value;
}

void term_setcolor(uint8_t color) {
   terminal_color = color;
}

void term_movecur(int row, int col)
{
   uint16_t position = (row * term_width) + col;

   // cursor LOW port to vga INDEX register
   outb(0x3D4, 0x0F);
   outb(0x3D5, (unsigned char)(position & 0xFF));
   // cursor HIGH port to vga INDEX register
   outb(0x3D4, 0x0E);
   outb(0x3D5, (unsigned char)((position >> 8) & 0xFF));
}

void term_init() {

   uint8_t defColor = make_color(COLOR_WHITE, COLOR_BLACK);
   term_movecur(0, 0);

   volatile uint16_t *ptr = TERMINAL_VIDEO_ADDR;

   for (int i = 0; i < term_width*term_height; ++i) {
      *ptr++ = make_vgaentry(' ', defColor);
   }

   term_setcolor(defColor);
}

static void increase_buf_next_slot(int val)
{
   if (val < 0) {
      buf_next_slot += val;

      if (buf_next_slot < 0)
         buf_next_slot += TERMINAL_BUFFER_ROWS;
      return;
   }

   if (buf_next_slot + val >= TERMINAL_BUFFER_ROWS) {  // we'll wrap around
      buf_full = true;
   }

   buf_next_slot = (buf_next_slot + val) % TERMINAL_BUFFER_ROWS;
}

static void from_buffer_to_video(int bufRow, int videoRow)
{
   if (bufRow < 0) {
      bufRow += TERMINAL_BUFFER_ROWS;
   } else {
      bufRow %= TERMINAL_BUFFER_ROWS;
   }

   memcpy((void *)(TERMINAL_VIDEO_ADDR + videoRow * term_width),
          (const void *)(TERMINAL_BUFFER_ADDR + bufRow * term_width), term_width * 2);
}

static void push_line_in_buffer(int videoRow)
{
   int destIndex = buf_next_slot % TERMINAL_BUFFER_ROWS;

   memcpy((void *)(TERMINAL_BUFFER_ADDR + destIndex * term_width),
          (const void *)(TERMINAL_VIDEO_ADDR + videoRow * term_width), term_width * 2);

   increase_buf_next_slot(1);
}

static void pop_line_from_buffer(int videoRow)
{
   ASSERT(buf_next_slot > 0);

   from_buffer_to_video(buf_next_slot - 1, videoRow);
   increase_buf_next_slot(-1);
}


void term_scroll(int lines)
{
   int max_scroll_lines = 0;

   if (lines < 0) {
      lines = 0;
   }

   if (lines == 0) {

      if (scroll_value == 0) {
         return;
      }

      // just restore the video buffer

      for (int i = 0; i < term_height; i++) {
         pop_line_from_buffer(term_height - i - 1);
      }

      scroll_value = 0;
      return;
   }


   max_scroll_lines = buf_full
                      ? TERMINAL_BUFFER_ROWS
                      : MIN(buf_next_slot, TERMINAL_BUFFER_ROWS);

   if (scroll_value == 0) {

      // if the current scroll_value is 0,
      // save the whole current screen buffer.

      for (int i = 0; i < term_height; i++) {
         push_line_in_buffer(i);
      }

   } else {

      max_scroll_lines -= term_height;
   }

   lines = MIN(lines, max_scroll_lines);

   for (int i = 0; i < term_height; i++) {

      from_buffer_to_video(buf_next_slot - 1 - lines - i,
                           term_height - i - 1);
   }

   scroll_value = lines;
}

static void term_incr_row()
{
   if (terminal_row < term_height - 1) {
      ++terminal_row;
      return;
   }

   push_line_in_buffer(0);

   // We have to scroll...

   memmove((void *) TERMINAL_VIDEO_ADDR,
           (const void *) (TERMINAL_VIDEO_ADDR + term_width),
           term_width * (term_height - 1) * 2);

   volatile uint16_t *lastRow =
      TERMINAL_VIDEO_ADDR + term_width * (term_height - 1);

   for (int i = 0; i < term_width; i++) {
      lastRow[i] = make_vgaentry(' ', terminal_color);
   }
}

void term_write_char(char c)
{
   if (scroll_value != 0) {
      term_scroll(0);
   }

   if (c == '\n') {
      terminal_column = 0;
      term_incr_row();
      term_movecur(terminal_row, terminal_column);
      return;
   }

   if (c == '\r') {
      terminal_column = 0;
      term_movecur(terminal_row, terminal_column);
      return;
   }

   if (c == '\t') {
      return;
   }

   volatile uint16_t *video = TERMINAL_VIDEO_ADDR;

   if (c == '\b') {

      if (terminal_column > 0) {
         --terminal_column;
      }

      const size_t offset = terminal_row * term_width + terminal_column;
      video[offset] = make_vgaentry(' ', terminal_color);

      term_movecur(terminal_row, terminal_column);
      return;
   }

   const size_t offset = terminal_row * term_width + terminal_column;
   video[offset] = make_vgaentry(c, terminal_color);
   ++terminal_column;

   if (terminal_column == term_width) {
      terminal_column = 0;
      term_incr_row();
   }

   term_movecur(terminal_row, terminal_column);
}

void term_write_string(const char *str)
{
   while (*str) {
      term_write_char(*str++);
   }
}

void term_move_ch(int row, int col)
{
   terminal_row = row;
   terminal_column = col;

   term_movecur(row, col);
}
