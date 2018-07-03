

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
#include "kb_layouts.h"

#define KB_TASKLETS_QUEUE_SIZE 128


typedef enum {

   KB_DEFAULT_STATE,
   KB_READ_E0_SCANCODE_STATE,
   KB_READ_E1_SCANCODE_STATE,
   KB_READ_FIRST_SCANCODE_AFTER_E1_STATE

} kb_state_t;

static kb_state_t kb_curr_state = KB_DEFAULT_STATE;

static int kb_tasklet_runner = -1;

static bool key_pressed_state[2][128];

static bool numLock = false;
static bool capsLock = false;

static inline bool is_pressed(u32 key)
{
   bool e0 = (key >> 8) == 0xE0;
   return key_pressed_state[e0][key & 0xFF];
}

static void numlock_set_led(bool val)
{
   kb_led_set(capsLock << 2 | val << 1);
}

static void capslock_set_led(bool val)
{
   kb_led_set(numLock << 1 | val << 2);
}

static u8 translate_printable_key(u32 key)
{
   u8 *layout =
      us_kb_layouts[is_pressed(KEY_L_SHIFT) || is_pressed(KEY_R_SHIFT)];

   u8 c = layout[key];

   if (numLock)
      c |= numkey[key];

   if (capsLock)
      c = toupper(c);

   return c;
}

/*
 * Condition variable on which tasks interested in keyboard input, wait.
 */
kcond kb_cond;

void handle_key_pressed(u32 key)
{
   switch(key) {

   case KEY_E0_PAGE_UP:
      term_scroll_up(5);
      return;

   case KEY_E0_PAGE_DOWN:
      term_scroll_down(5);
      return;

   case KEY_E0_DEL:

      if (is_pressed(KEY_CTRL) && is_pressed(KEY_ALT)) {
         printk("Ctrl + Alt + Del: Reboot!\n");
         reboot();
      }

      return;

   case NUM_LOCK:
      numLock = !numLock;
      numlock_set_led(numLock);
      printk("\nNUM LOCK is %s\n", numLock ? "ON" : "OFF");
      return;

   case CAPS_LOCK:
      capsLock = !capsLock;
      capslock_set_led(capsLock);
      printk("\nCAPS LOCK is %s\n", capsLock ? "ON" : "OFF");
      return;

   case KEY_L_SHIFT:
   case KEY_R_SHIFT:
      return;

   case KEY_F1:
      debug_show_spurious_irq_count();
      return;

   case KEY_F2:
      debug_kmalloc_dump_mem_usage();
      return;

   case KEY_F3:
      debug_term_print_scroll_cycles();
      return;

   default:
      break;
   }

   if ((key >> 8) == 0xE0) {
      printk("PRESSED key: 0x%x\n", key);
      return;
   }

   u8 c = translate_printable_key(key);

   if (!c) {
      printk("PRESSED key: 0x%x\n", key);
      return;
   }

   if (c == '\b') {

      if (kb_cbuf_drop_last_written_elem(NULL))
         term_write((char *)&c, 1);

      return;
   }

   /* Default case: a regular (printable) character */

   if (kb_cbuf_write_elem(c)) {

      term_write((char *)&c, 1);

      if (c == '\n' || kb_cbuf_is_full()) {
         kcond_signal_one(&kb_cond);
      }
   }
}

static void key_int_handler(u32 key, bool is_pressed)
{
   bool e0 = (key >> 8) == 0xE0;
   key_pressed_state[e0][key & 0xFF] = is_pressed;

   if (is_pressed) {
      handle_key_pressed(key);
   }
}

static void kb_handle_default_state(u8 scancode)
{
   switch (scancode) {

      case 0xE0:
         kb_curr_state = KB_READ_E0_SCANCODE_STATE;
         break;

      case 0xE1:
         kb_curr_state = KB_READ_E1_SCANCODE_STATE;
         break;

      default:
         key_int_handler(scancode & ~0x80, !(scancode & 0x80));
   };
}

static void kb_tasklet_handler(u8 scancode)
{
   bool is_pressed;

   switch (kb_curr_state) {

      case KB_READ_FIRST_SCANCODE_AFTER_E1_STATE:
         /* We ignore 0xE1 sequences at the moment (scancode 2/2) */
         kb_curr_state = KB_DEFAULT_STATE;
         break;

      case KB_READ_E1_SCANCODE_STATE:
         /* We ignore 0xE1 sequences at the moment (scancode 1/2) */
         kb_curr_state = KB_READ_FIRST_SCANCODE_AFTER_E1_STATE;
         break;

      case KB_READ_E0_SCANCODE_STATE:

         kb_curr_state = KB_DEFAULT_STATE;

         // Fake lshift pressed (2A) or released (AA)
         if (scancode == 0x2A || scancode == 0xAA)
            break;

         is_pressed = !(scancode & 0x80);
         scancode &= ~0x80;

         key_int_handler(scancode | (0xE0 << 8), is_pressed);
         break;

      case KB_DEFAULT_STATE:
         kb_handle_default_state(scancode);
         break;
   };
}

static int keyboard_irq_handler(regs *context)
{
   u8 scancode;
   ASSERT(are_interrupts_enabled());
   ASSERT(!is_preemption_enabled());

   if (!kb_wait_cmd_fetched())
      panic("KB: fatal error: timeout in kb_wait_cmd_fetched");

   if (!kb_ctrl_is_pending_data())
      return 0;

   /* Read from the keyboard's data buffer */
   scancode = inb(KB_DATA_PORT);

   if (!enqueue_tasklet1(kb_tasklet_runner, &kb_tasklet_handler, scancode))
      panic("KB: hit tasklet queue limit");

   return 1;
}

/* This will be executed in a tasklet */
void init_kb(void)
{
   disable_preemption();

   ringbuf_init(&kb_cooked_ringbuf, KB_CBUF_SIZE, 1, kb_cooked_buf);
   kcond_init(&kb_cond);

   if (!kb_ctrl_self_test()) {

      printk("Warning: PS/2 controller self-test failed, trying a reset\n");

      if (!kb_ctrl_reset())
         panic("Unable to initialize the PS/2 controller");

      printk("PS/2 controller: reset successful\n");
   }

   numlock_set_led(numLock);
   capslock_set_led(capsLock);
   kb_set_typematic_byte(0);

   kb_tasklet_runner =
      create_tasklet_thread(1 /* priority */, KB_TASKLETS_QUEUE_SIZE);

   if (kb_tasklet_runner < 0)
      panic("KB: Unable to tasklet runner thread for IRQs");

   irq_install_handler(X86_PC_KEYBOARD_IRQ, keyboard_irq_handler);
   enable_preemption();

   printk("keyboard initialized.\n");
}
