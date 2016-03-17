
#include <commonDefs.h>
#include <stringUtil.h>
#include <term.h>

#define KB_DATA_PORT 0x60
#define KB_CONTROL_PORT 0x64

/* KBDUS means US Keyboard Layout. This is a scancode table
*  used to layout a standard US keyboard. I have left some
*  comments in to give you an idea of what key is what, even
*  though I set it's array index to 0. You can change that to
*  whatever you want using a macro, if you wish! */
unsigned char kbdus[128] =
{
   0,  27, '1', '2', '3', '4', '5', '6', '7', '8', /* 9 */
   '9', '0', '-', '=', '\b',  /* Backspace */
   '\t',       /* Tab */
   'q', 'w', 'e', 'r',  /* 19 */
   't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',   /* Enter key */
   0,       /* 29   - Control */
   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',  /* 39 */
   '\'', '`',   0,      /* Left shift */
   '\\', 'z', 'x', 'c', 'v', 'b', 'n',       /* 49 */
   'm', ',', '.', '/',   0,            /* Right shift */
   '*',
   0, /* Alt */
   ' ',  /* Space bar */
   0, /* Caps lock */
   0, /* 59 - F1 key ... > */
   0,   0,   0,   0,   0,   0,   0,   0,
   0, /* < ... F10 */
   0, /* 69 - Num lock*/
   0, /* Scroll Lock */
   0, /* Home key */
   0, /* Up Arrow */
   0, /* Page Up */
   '-',
   0, /* Left Arrow */
   0,
   0, /* Right Arrow */
   '+',
   0, /* 79 - End key*/
   0, /* Down Arrow */
   0, /* Page Down */
   0, /* Insert Key */
   0, /* Delete Key */
   0,   0,   0,
   0, /* F11 Key */
   0, /* F12 Key */
   0, /* All other keys are undefined */
};

unsigned char kbdus_up[128] =
{
   0,  27, '1', '2', '3', '4', '5', '6', '7', '8', /* 9 */
   '9', '0', '-', '=', '\b',  /* Backspace */
   '\t',       /* Tab */
   'Q', 'W', 'E', 'R',  /* 19 */
   'T', 'Y', 'U', 'I', 'O', 'P', '[', ']', '\n',   /* Enter key */
   0,       /* 29   - Control */
   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ';',  /* 39 */
   '\'', '`',   0,      /* Left shift */
   '\\', 'Z', 'X', 'C', 'V', 'B', 'N',       /* 49 */
   'M', ',', '.', '/',   0,            /* Right shift */
   '*',
   0, /* Alt */
   ' ',  /* Space bar */
   0, /* Caps lock */
   0, /* 59 - F1 key ... > */
   0,   0,   0,   0,   0,   0,   0,   0,
   0, /* < ... F10 */
   0, /* 69 - Num lock*/
   0, /* Scroll Lock */
   0, /* Home key */
   0, /* Up Arrow */
   0, /* Page Up */
   '-',
   0, /* Left Arrow */
   0,
   0, /* Right Arrow */
   '+',
   0, /* 79 - End key*/
   0, /* Down Arrow */
   0, /* Page Down */
   0, /* Insert Key */
   0, /* Delete Key */
   0,   0,   0,
   0, /* F11 Key */
   0, /* F12 Key */
   0, /* All other keys are undefined */
};

#define KEY_L_SHIFT 42
#define KEY_R_SHIFT 54



uint8_t pressed_keys[128] = { 0 };

void keyboard_handler(struct regs *r)
{
   unsigned char scancode;


   while ((inb(KB_CONTROL_PORT) & 2) != 0) {
      //check if scancode is ready
      //this is useful since sometimes the IRQ is triggered before
      //the data is available.
   }

   /* Read from the keyboard's data buffer */
   scancode = inb(KB_DATA_PORT);

   /* If the top bit of the byte we read from the keyboard is
   *  set, that means that a key has just been released */
   if (scancode & 0x80)
   {
      /* You can use this one to see if the user released the
      *  shift, alt, or control keys... */

      pressed_keys[scancode & ~0x80] = 0;
      
      //printk("RELEASED scancode: %i\n", scancode & ~0x80);
   }
   else
   {
      /* Here, a key was just pressed. Please note that if you
      *  hold a key down, you will get repeated key press
      *  interrupts. */

      /* Just to show you how this works, we simply translate
      *  the keyboard scancode into an ASCII value, and then
      *  display it to the screen. You can get creative and
      *  use some flags to see if a shift is pressed and use a
      *  different layout, or you can add another 128 entries
      *  to the above layout to correspond to 'shift' being
      *  held. If shift is held using the larger lookup table,
      *  you would add 128 to the scancode when you look for it */

      

      pressed_keys[scancode] = 1;
      
      //printk("PRESSED scancode: %i\n", scancode);
      
      
      
      if (pressed_keys[KEY_L_SHIFT] || pressed_keys[KEY_R_SHIFT]) {
         unsigned char c = kbdus_up[scancode];
         if (c) term_write_char(c);
      } else {
         unsigned char c = kbdus[scancode];
         if (c) term_write_char(c);
      }
   }
}

