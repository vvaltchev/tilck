
#include <commonDefs.h>
#include <stringUtil.h>
#include <term.h>

#define KB_DATA_PORT 0x60
#define KB_CONTROL_PORT 0x64

/* US Keyboard Layout.  */
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
   0,   0,   '\\',
   0, /* F11 Key */
   0, /* F12 Key */
   0, /* All other keys are undefined */
};

unsigned char kbdus_up[128] =
{
   0,  27, '!', '@', '#', '$', '%', '^', '&', '*', /* 9 */
   '(', ')', '_', '+', '\b',  /* Backspace */
   '\t',       /* Tab */
   'Q', 'W', 'E', 'R',  /* 19 */
   'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',   /* Enter key */
   0,       /* 29   - Control */
   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',  /* 39 */
   '\"', '~',   0,      /* Left shift */
   '|', 'Z', 'X', 'C', 'V', 'B', 'N',       /* 49 */
   'M', '<', '>', '?',   0,            /* Right shift */
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
   0,   0,  '|',
   0, /* F11 Key */
   0, /* F12 Key */
   0, /* All other keys are undefined */
};

uint8_t numkey[128] = {
   [71] = '7', '8', '9',
   [75] = '4', '5', '6',
   [79] = '1', '2', '3'
};

#define KEY_L_SHIFT 42
#define KEY_R_SHIFT 54
#define NUM_LOCK 69

bool pressed_keys[128] = { false };
bool blockNum = true;


//void kbd_wait_asm()
//{
//   uint8_t al;
//
//begin:
//
//   al = inb(KB_CONTROL_PORT);
//
//   if ((al & 1) == 0) {
//      goto ok;
//   }
//
//   (void) inb(KB_DATA_PORT);
//   goto begin;
//
//ok:
//
//   if ((al & 2) != 0) {
//      // we have data, drain it.
//      goto begin;
//   }
//}

void kbd_wait()
{
   uint8_t al;

   do {

      while ((al = inb(KB_CONTROL_PORT)) & 1) {
         (void) inb(KB_DATA_PORT);
      }

   } while (al & 2);
}

void num_lock_switch(bool val)
{
   kbd_wait();

   outb(KB_DATA_PORT, 0xED);
   kbd_wait();

   outb(KB_DATA_PORT, 0x0 | (!!val << 1));
   kbd_wait();
}

void init_kb()
{
   num_lock_switch(blockNum);
}

void keyboard_handler()
{
   unsigned char scancode;


   while (inb(KB_CONTROL_PORT) & 2) {
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
      pressed_keys[scancode & ~0x80] = false;

      //printk("RELEASED scancode: %i\n", scancode & ~0x80);
   } else {

      pressed_keys[scancode] = true;

      if (scancode == NUM_LOCK) {
         blockNum = !blockNum;
         num_lock_switch(blockNum);
         printk("NUM LOCK is %s\n", blockNum ? "ON" : "OFF");
         return;
      }

      //printk("PRESSED scancode: %i\n", scancode);
      
      unsigned char c;
      
      if (pressed_keys[KEY_L_SHIFT] || pressed_keys[KEY_R_SHIFT]) {
         c = kbdus_up[scancode];
      } else {
         c = kbdus[scancode];
      }

      if (blockNum) {
         c |= numkey[scancode];
      }

      if (c) {
         term_write_char(c);
      } else {
         printk("PRESSED scancode: %i\n", scancode);
      }
   }
}

