

/*
 * This is a DEMO/DEBUG version of the KB driver good enough ONLY for basic
 * experiments.
 *
 * TODO: reimplement this driver in a proper way. As per today, this is the
 * oldest (and NOT-refactored) code in exOS, by far worse than anything else in
 * the project.
 *
 *
 * Useful info:
 * http://wiki.osdev.org/PS/2_Keyboard
 * http://www.brokenthorn.com/Resources/OSDev19.html
 *
 */


#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/term.h>
#include <exos/tasklet.h>
#include <exos/hal.h>
#include <exos/process.h>
#include <exos/sync.h>
#include <exos/ringbuf.h>
#include <exos/timer.h>

#include "kb_int_cbuf.h"
#include "kb_int.h"

/* US Keyboard Layout.  */
static u8 kbd_us[128] =
{
   0,  27, '1', '2', '3', '4', '5', '6', '7', '8',
   '9', '0', '-', '=', '\b',  /* Backspace */
   '\t',       /* Tab */
   'q', 'w', 'e', 'r',  /* 19 */
   't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
   0,       /* 29   - Control */
   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',  /* 39 */
   '\'', '`',
   0,      /* Left shift */
   '\\', 'z', 'x', 'c', 'v', 'b', 'n',       /* 49 */
   'm', ',', '.', '/',
   0,            /* Right shift */
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

static u8 kbd_us_up[128] =
{
   0,  27, '!', '@', '#', '$', '%', '^', '&', '*',
   '(', ')', '_', '+', '\b',
   '\t',       /* Tab */
   'Q', 'W', 'E', 'R',  /* 19 */
   'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
   0,       /* 29   - Control */
   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',  /* 39 */
   '\"', '~',
   0,      /* Left shift */
   '|', 'Z', 'X', 'C', 'V', 'B', 'N',       /* 49 */
   'M', '<', '>', '?',
   0,            /* Right shift */
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

#define KEY_F1  (0x3b)
#define KEY_F2  (KEY_F1 + 1)
#define KEY_F3  (KEY_F1 + 2)
#define KEY_F4  (KEY_F1 + 3)
#define KEY_F5  (KEY_F1 + 4)
#define KEY_F6  (KEY_F1 + 5)
#define KEY_F7  (KEY_F1 + 6)
#define KEY_F8  (KEY_F1 + 7)
#define KEY_F9  (KEY_F1 + 8)
#define KEY_F10 (KEY_F1 + 9)

static unsigned char *us_kb_layouts[2] = {
   kbd_us, kbd_us_up
};

static u8 numkey[128] = {
   [71] = '7', '8', '9',
   [75] = '4', '5', '6',
   [79] = '1', '2', '3',
   [82] = '0', '.'
};

static bool pkeys[128];
static bool e0pkeys[128];
static bool *pkeysArrays[2] = { pkeys, e0pkeys };

static bool numLock = false;
static bool capsLock = false;
static bool lastWasE0 = false;

static u8 next_kb_interrupts_to_ignore = 0; // HACK to skip 0xE1 sequences

static void kbd_wait(void)
{
   u8 temp;

   /* Clear all keyboard buffers (output and command buffers) */
   do
   {
      temp = inb(KB_CONTROL_PORT);

      if (temp & KB_CTRL_OUTPUT_FULL) {
         inb(KB_DATA_PORT);
      }

   } while (temp & KB_CTRL_INPUT_FULL);
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

/*
 * Condition variable on which tasks interested in keyboard input, wait.
 */
kcond kb_cond;

void print_slow_timer_handler_counter(void);
void debug_term_print_scroll_cycles(void);
extern u32 spur_irq_count;

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

   case KEY_F1:

#if KERNEL_TRACK_NESTED_INTERRUPTS
      print_slow_timer_handler_counter();
#endif

      if (get_ticks() > TIMER_HZ)
         printk("Spur IRQ count: %u (%u/sec)\n",
               spur_irq_count,
               spur_irq_count / (get_ticks() / TIMER_HZ));
      else
         printk("Spurious IRQ count: %u (< 1 sec)\n",
               spur_irq_count, spur_irq_count);

      return;

   case KEY_F2:
      debug_kmalloc_dump_mem_usage();
      return;

   case KEY_F3:
      debug_term_print_scroll_cycles();
      return;

   case KEY_F4:
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
      c = toupper(c);
   }

   if (!c) {
      printk("PRESSED scancode: 0x%x (%i)\n", scancode, scancode);
      return;
   }

   if (c != '\b') {

      if (kb_cbuf_write_elem(c)) {
         term_write((char *)&c, 1);

         if (c == '\n' || kb_cbuf_is_full()) {
            bool success = enqueue_tasklet1(kcond_signal_one, &kb_cond);
            VERIFY(success); // TODO: any better way to handle this?
         }
      }

   } else {

      // c == '\b'

      if (kb_cbuf_drop_last_written_elem(NULL))
         term_write((char *)&c, 1);
   }
}

void handle_E0_key_pressed(u8 scancode)
{
   switch (scancode) {

   case KEY_PAGE_UP:
      term_scroll_up(5);
      break;

   case KEY_PAGE_DOWN:
      term_scroll_down(5);
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

static void (*keyPressHandlers[2])(u8) = {
   handle_key_pressed, handle_E0_key_pressed
};



void keyboard_handler()
{
   u8 scancode;
   ASSERT(are_interrupts_enabled());

   if (!kb_wait_cmd_fetched())
      panic("KB: fatal error: timeout in kb_wait_cmd_fetched");

   if (!kb_ctrl_is_pending_data())
      return;

   /* Read from the keyboard's data buffer */
   scancode = inb(KB_DATA_PORT);

   // Hack used to avoid handling 0xE1 two-scancode sequences
   if (next_kb_interrupts_to_ignore) {
      next_kb_interrupts_to_ignore--;
      return;
   }

   // Hack used to avoid handling 0xE1 two-scancode sequences
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

   int is_pressed = !(scancode & 0x80);
   scancode &= ~0x80;

   pkeysArrays[lastWasE0][scancode] = is_pressed;

   if (is_pressed)
      keyPressHandlers[lastWasE0](scancode);

end:
   lastWasE0 = false;
}

// Reboot procedure using the keyboard controller

void reboot(void)
{
   disable_interrupts_forced(); /* Disable the interrupts before rebooting */
   kbd_wait();

   outb(KB_CONTROL_PORT, KB_CMD_CPU_RESET);

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
void init_kb(void)
{
   disable_preemption();

   ringbuf_init(&kb_cooked_ringbuf, KB_CBUF_SIZE, 1, kb_cooked_buf);
   kcond_init(&kb_cond);

   if (!kb_ctrl_self_test()) {
      if (!kb_ctrl_reset())
         panic("Unable to initialize the keyboard controller");
   }

   num_lock_switch(numLock);
   caps_lock_switch(capsLock);
   kb_set_typematic_byte(0);

   printk("keyboard initialized.\n");
   enable_preemption();
}
