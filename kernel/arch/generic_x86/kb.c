

/*
 * This is a DEMO/DEBUG version of the KB driver.
 * Until the concept of character devices is implemented in exOS, that's
 * good enough for basic experiments.
 *
 * Useful info:
 * http://wiki.osdev.org/PS/2_Keyboard
 * http://www.brokenthorn.com/Resources/OSDev19.html
 *
 */


#include <common_defs.h>
#include <string_util.h>
#include <term.h>
#include <tasklet.h>
#include <hal.h>

#define KB_DATA_PORT 0x60
#define KB_CONTROL_PORT 0x64

/* keyboard interface bits */
#define KBRD_BIT_KDATA 0     // keyboard data is in buffer
                             // (output buffer is empty) (bit 0)

#define KBRD_BIT_UDATA 1     // user data is in buffer
                             // (command buffer is empty) (bit 1)

#define KBRD_RESET 0xFE /* reset CPU command */

#define KB_ACK 0xFA
#define KB_RESEND 0xFE

#define BIT(n) (1 << (n))
#define CHECK_FLAG(flags, n) ((flags) & BIT(n))

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

unsigned char *us_kb_layouts[2] = {
   kbdus, kbdus_up
};

u8 numkey[128] = {
   [71] = '7', '8', '9',
   [75] = '4', '5', '6',
   [79] = '1', '2', '3',
   [82] = '0', '.'
};

#define KEY_L_SHIFT 42
#define KEY_R_SHIFT 54
#define NUM_LOCK 69
#define CAPS_LOCK 58
#define KEY_PAGE_UP 0x49
#define KEY_PAGE_DOWN 0x51
#define KEY_LEFT 0x4b
#define KEY_RIGHT 0x4d
#define KEY_UP 0x48
#define KEY_DOWN 0x50

#define KEY_CTRL 0x1d
#define KEY_ALT 0x38
#define KEY_E0_DEL 0x53

bool pkeys[128] = { false };
bool e0pkeys[128] = { false };

bool *pkeysArrays[2] = { pkeys, e0pkeys };

bool numLock = false;
bool capsLock = false;
bool lastWasE0 = false;

u8 next_kb_interrupts_to_ignore = 0;

void kbd_wait()
{
   u8 temp;

   /* Clear all keyboard buffers (output and command buffers) */
   do
   {
      temp = inb(KB_CONTROL_PORT); /* empty user data */
      if (CHECK_FLAG(temp, KBRD_BIT_KDATA) != 0) {
         inb(KB_DATA_PORT); /* empty keyboard data */
      }
   } while (CHECK_FLAG(temp, KBRD_BIT_UDATA) != 0);
}

void kb_led_set(u8 val)
{
   kbd_wait();

   outb(KB_DATA_PORT, 0xED);
   kbd_wait();

   outb(KB_DATA_PORT, val & 7);
   kbd_wait();
}

void num_lock_switch(bool val)
{
   kb_led_set(capsLock << 2 | val << 1);
}

void caps_lock_switch(bool val)
{
   kb_led_set(numLock << 1 | val << 2);
}

//////////////////////////////////////////
// TEMP EXPERIMENT STUFF

void dummy_tasklet(int arg1)
{
   //printk(" ### Running dummy_tasklet, key = '%c' ###\n", arg1);
   term_write_char(arg1);
}

void panic_tasklet()
{
   NOT_REACHED();
}

//////////////////////////////////////////

void handle_key_pressed(u8 scancode)
{
   switch(scancode) {

   case NUM_LOCK:
      numLock = !numLock;
      num_lock_switch(numLock);
      printk("\nNUM LOCK is %s\n", numLock ? "ON" : "OFF");
      return;

   case CAPS_LOCK:
      capsLock = !capsLock;
      caps_lock_switch(capsLock);
      printk("\nCAPS LOCK is %s\n", capsLock ? "ON" : "OFF");
      return;

   case KEY_L_SHIFT:
   case KEY_R_SHIFT:
      return;

   default:
      break;
   }

   u8 *layout = us_kb_layouts[pkeys[KEY_L_SHIFT] || pkeys[KEY_R_SHIFT]];
   u8 c = layout[scancode];

   if (numLock) {
      c |= numkey[scancode];
   }

   if (capsLock) {
      c = upper(c);
   }

   if (c) {

      if (c == '!' || c == '@') {

         //printk("you pressed char '%c' creating tasklet\n", c);
         bool success;
         success = enqueue_tasklet1(&dummy_tasklet, c);

         if (!success) {
            printk("[kb] failed to create tasklet\n");
         }

      } else if (c == '$') {

         enqueue_tasklet0(panic_tasklet);

      } else {
         term_write_char(c);
      }

   } else {
      printk("PRESSED scancode: 0x%x (%i)\n", scancode, scancode);
   }
}

void handle_E0_key_pressed(u8 scancode)
{
   switch (scancode) {

   case KEY_PAGE_UP:
      term_scroll(term_get_scroll_value() + 5);
      break;

   case KEY_PAGE_DOWN:
      term_scroll(term_get_scroll_value() - 5);
      break;

   case KEY_E0_DEL:

      if (pkeysArrays[0][KEY_CTRL] && pkeysArrays[0][KEY_ALT]) {
         printk("Ctrl + Alt + Del: Reboot!\n");
         reboot();
         break;
      }

   // fall-through

   default:
      printk("PRESSED E0 scancode: 0x%x (%i)\n", scancode, scancode);
      break;
   }
}

void (*keyPressHandlers[2])(u8) = {
   handle_key_pressed, handle_E0_key_pressed
};

//extern volatile u64 jiffies;

void keyboard_handler()
{
   u8 scancode;
   ASSERT(are_interrupts_enabled());
   // printk("keyboard handler begin (ticks: %llu) <", jiffies);

   // for (int i = 0; i < 100*1000*1000; i++) {
   //    asmVolatile("nop");
   // }

   while (inb(KB_CONTROL_PORT) & 2) {
      //check if scancode is ready
      //this is useful since sometimes the IRQ is triggered before
      //the data is available.
   }

   /* Read from the keyboard's data buffer */
   scancode = inb(KB_DATA_PORT);

   if (next_kb_interrupts_to_ignore) {
      next_kb_interrupts_to_ignore--;
      return;
   }

   if (scancode == 0xE1) {
      next_kb_interrupts_to_ignore = 2;
      return;
   }


   if (scancode == 0xE0) {
      lastWasE0 = true;
      return;
   }


   if (lastWasE0) {
      // Fake lshift pressed (2A) or released (AA)
      if (scancode == 0x2A || scancode == 0xAA) {
         goto end;
      }
   }

   if (scancode & 0x80) {

      pkeysArrays[lastWasE0][scancode & ~0x80] = false;

   } else {

      pkeysArrays[lastWasE0][scancode] = true;
      keyPressHandlers[lastWasE0](scancode);
   }

end:
   lastWasE0 = false;

   //printk("> END.\n");
}

// Reboot procedure using the keyboard controller

void reboot() {

   disable_interrupts();
   kbd_wait();

   outb(KB_CONTROL_PORT, KBRD_RESET);

   while (true) {
      halt();
   }
}


/*
 * From http://wiki.osdev.org/PS/2_Keyboard
 *
 * BITS [0,4]: Repeat rate (00000b = 30 Hz, ..., 11111b = 2 Hz)
 * BITS [5,6]: Delay before keys repeat (00b = 250 ms, ..., 11b = 1000 ms)
 * BIT [7]: Must be zero
 * BIT [8]: I'm assuming is ignored.
 *
 */

void kb_set_typematic_byte(u8 val)
{
   kbd_wait();
   outb(KB_DATA_PORT, 0xF3);
   kbd_wait();
   outb(KB_DATA_PORT, 0);
   kbd_wait();
}

/* This will be executed in a tasklet */
void init_kb()
{
   disable_preemption();

   num_lock_switch(numLock);
   caps_lock_switch(capsLock);
   kb_set_typematic_byte(0);

   printk("[kernel] keyboard initialized.\n");

   enable_preemption();
}
