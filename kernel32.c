
#define asm __asm__

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long size_t;


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
 
volatile uint8_t terminal_row;
volatile uint8_t terminal_column;
volatile uint8_t terminal_color;

void terminal_setcolor(uint8_t color) {
   terminal_color = color;
}


///////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////

#define TERMINAL_VIDEO_ADDR ((volatile uint8_t*)0xB8000)

static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
    /* There's an outb %al, $imm8  encoding, for compile-time constant port numbers that fit in 8b.  (N constraint).
     * Wider immediate constants would be truncated at assemble-time (e.g. "i" constraint).
     * The  outb  %al, %dx  encoding is the only option for all other cases.
     * %1 expands to %dx because  port  is a uint16_t.  %w1 could be used if we had the port number a wider C type */
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret_val;
    asm volatile ( "inb %[port], %[result]"
                   : [result] "=a"(ret_val)   // using symbolic operand names as an example, mainly because they're not used in order
                   : [port] "Nd"(port) );
    return ret_val;
}

 /* void update_cursor(int row, int col)
  * by Dark Fiber
  */
 void update_cursor(int row, int col)
 {
    unsigned short position = (row*80) + col;
 
    // cursor LOW port to vga INDEX register
    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(position&0xFF));
    // cursor HIGH port to vga INDEX register
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char )((position>>8)&0xFF));
 }

void term_init() {
   
   terminal_row = 0;
   terminal_column = 0;
   update_cursor(0, 0);
   
   terminal_setcolor(make_color(COLOR_GREEN, COLOR_BLACK));
   volatile uint16_t *ptr = (volatile uint16_t *)TERMINAL_VIDEO_ADDR;
   
   for (int i = 0; i < TERM_WIDTH*TERM_HEIGHT; ++i) {
      *ptr++ = make_vgaentry(' ', terminal_color);
   }
}

void term_write_char(char c) {
   
   if (c == '\n') {
      terminal_column = 0;
      terminal_row++;
      update_cursor(terminal_row, terminal_column);
      return;
   }
   
   if (c == '\r') {
      terminal_column = 0;
      update_cursor(terminal_row, terminal_column);
      return;
   }
   
   volatile uint16_t *video = (volatile uint16_t *)TERMINAL_VIDEO_ADDR;   
   const size_t offset = terminal_row * TERM_WIDTH + terminal_column;
   video[offset] = make_vgaentry(c, terminal_color);

   ++terminal_column;
   
   if (terminal_column == TERM_WIDTH) {
      terminal_column = 0;
      terminal_row++;
   }

   update_cursor(terminal_row, terminal_column);   
}

void write_string(const char *str)
{
   while (*str) {
      term_write_char(*str++);
   }
}

void term_move_ch(int row, int col)
{
   terminal_row = row;
   terminal_column = col;
}

char * itoa( int value, char * str, int base )
{
   char * rc;
   char * ptr;
   char * low;
   // Check for supported base.
   if ( base < 2 || base > 36 )
   {
     *str = '\0';
     return str;
   }
   rc = ptr = str;
   // Set '-' for negative decimals.
   if ( value < 0 && base == 10 )
   {
     *ptr++ = '-';
   }
   // Remember where the numbers start.
   low = ptr;
   // The actual conversion.
   do
   {
     // Modulo is negative for negative value. This trick makes abs() unnecessary.
     *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + value % base];
     value /= base;
   } while ( value );
   // Terminating the string.
   *ptr-- = '\0';
   // Invert the numbers.
   while ( low < ptr )
   {
     char tmp = *low;
     *low++ = *ptr;
     *ptr-- = tmp;
   }
   return rc;
}

void show_hello_message()
{
   term_move_ch(0, 0);
   term_write_char('*');

   term_move_ch(0, 79);
   term_write_char('*');
   
   term_move_ch(24, 0);
   term_write_char('*');

   term_move_ch(24, 79);
   term_write_char('*');
   
   term_move_ch(1, 0);
   write_string(" hello from my kernel!\n");
   write_string(" kernel, line 2");   
}

void kmain() {
   
   term_init(); 
   
   show_hello_message();
    
   while (1) { }
}