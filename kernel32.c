
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long size_t;

void kernelMain();

void _start() {
   kernelMain();
}

/* Hardware text mode color constants. */
enum vga_color {
   COLOR_BLACK = 0,
   COLOR_BLUE = 1,
   COLOR_GREEN = 2,
   COLOR_CYAN = 3,
   COLOR_RED = 4,
   COLOR_MAGENTA = 5,
   COLOR_BROWN = 6,
   COLOR_LIGHT_GREY = 7,
   COLOR_DARK_GREY = 8,
   COLOR_LIGHT_BLUE = 9,
   COLOR_LIGHT_GREEN = 10,
   COLOR_LIGHT_CYAN = 11,
   COLOR_LIGHT_RED = 12,
   COLOR_LIGHT_MAGENTA = 13,
   COLOR_LIGHT_BROWN = 14,
   COLOR_WHITE = 15,
};
 
uint8_t make_color(enum vga_color fg, enum vga_color bg) {
   return fg | bg << 4;
}
 
uint16_t make_vgaentry(char c, uint8_t color) {
   uint16_t c16 = c;
   uint16_t color16 = color;
   return c16 | color16 << 8;
}
 
 
static const size_t TERM_WIDTH = 80;
static const size_t TERM_HEIGHT = 25;
 
//size_t terminal_row;
//size_t terminal_column;
uint8_t terminal_color;

void terminal_setcolor(uint8_t color) {
   terminal_color = color;
}


///////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////

#define TERMINAL_VIDEO_ADDR ((volatile uint8_t*)0xB8000)

void term_init() {
   
   //terminal_row = 0;
   //terminal_column = 0;
   
   terminal_setcolor(make_color(COLOR_GREEN, COLOR_BLACK));
   volatile uint16_t *ptr = (volatile uint16_t *)TERMINAL_VIDEO_ADDR;
   
   for (int i = 0; i < TERM_WIDTH*TERM_HEIGHT; ++i) {
      *ptr++ = make_vgaentry('*', terminal_color);
   }
}

void write_string(const char *str) {
   
   const char *ptr = str;
   volatile uint16_t *video = (volatile uint16_t *)TERMINAL_VIDEO_ADDR;
   
   size_t terminal_row = 0;
   size_t terminal_column = 0;   
   
   uint8_t color = make_color(COLOR_GREEN, COLOR_BLACK);
   
   while (*ptr) {
      
      const size_t offset = terminal_row * TERM_WIDTH + terminal_column;
      video[offset] = make_vgaentry(*ptr, color);
      
      ++ptr;
      ++terminal_column;
      
      if (terminal_column == TERM_WIDTH) {
         terminal_column = 0;
         terminal_row++;
      }
   }
}

uint8_t mybuffer[16] =
{
  0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
  0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC,
  0xDD, 0xEE, 0xFF
};

void kernelMain() {
   
   //term_init();
   
   //terminal_initialize();
   //terminal_setcolor(make_color(COLOR_GREEN, COLOR_BLACK));
   //terminal_writestring("Hello from my C kernel!");
   
   write_string("hello from my kernel!   ");
    
   for (int i = 0; i < 16; i++) {
      mybuffer[i]--;
   }
    
   while (1) { }
}