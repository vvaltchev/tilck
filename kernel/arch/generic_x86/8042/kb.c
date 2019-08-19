/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/irq.h>

#include "kb_int.c.h"
#include "kb_layouts.c.h"
#include "kb_ansi_seq.c.h"

#define KB_TASKLETS_QUEUE_SIZE 128

enum kb_state {

   KB_DEFAULT_STATE = 0,
   KB_READ_E0_SCANCODE_STATE,
   KB_READ_E1_SCANCODE_STATE,
   KB_READ_FIRST_SCANCODE_AFTER_E1_STATE

};

int kb_tasklet_runner = -1;
static enum kb_state kb_curr_state;
static bool key_pressed_state[2][128];
static bool numLock = true;
static bool capsLock = false;
static list keypress_handlers = make_list(keypress_handlers);

bool kb_is_pressed(u32 key)
{
   bool e0 = (key >> 8) == 0xE0;
   return key_pressed_state[e0][key & 0xFF];
}

static inline void kb_led_update(void)
{
   kb_led_set((u8)(capsLock << 2 | numLock << 1));
}

static u8 translate_printable_key(u32 key)
{
   if (key >= 256) {

      switch (key) {

         case KEY_NUMPAD_ENTER:
            return '\r';

         case KEY_NUMPAD_SLASH:
            return '/';

         default:
            return 0;
      }
   }

   u8 *layout =
      us_kb_layouts[kb_is_pressed(KEY_L_SHIFT) || kb_is_pressed(KEY_R_SHIFT)];

   u8 c = layout[key];

   if (numLock)
      c |= numkey[key];

   if (capsLock)
      c = (u8) toupper(c);

   return c;
}

void kb_register_keypress_handler(keypress_handler_elem *e)
{
   list_node_init(&e->node);
   list_add_tail(&keypress_handlers, &e->node);
}

static int kb_call_keypress_handlers(u32 raw_key, u8 printable_char)
{
   int count = 0;
   keypress_handler_elem *pos;

   list_for_each_ro(pos, &keypress_handlers, node) {

      int rc = pos->handler(raw_key, printable_char);

      switch (rc) {
         case KB_HANDLER_OK_AND_STOP:
            count++;
            return count;

         case KB_HANDLER_OK_AND_CONTINUE:
            count++;
            break;

         case KB_HANDLER_NAK:
            break;

         default:
            NOT_REACHED();
      }
   }

   return count;
}

void handle_key_pressed(u32 key)
{
   switch(key) {

   case KEY_DEL:

      if (kb_is_pressed(KEY_LEFT_CTRL) && kb_is_pressed(KEY_LEFT_ALT)) {
         printk("Ctrl + Alt + Del: Reboot!\n");
         reboot();
      }

      break;

   case KEY_NUM_LOCK:
      numLock = !numLock;
      kb_led_update();
      return;

   case KEY_CAPS_LOCK:
      capsLock = !capsLock;
      kb_led_update();
      return;
   }

   //int hc =
   kb_call_keypress_handlers(key, translate_printable_key(key));

   // if (!hc && key != KEY_L_SHIFT && key != KEY_R_SHIFT)
   //    if (key != KEY_LEFT_CTRL && key != KEY_RIGHT_CTRL)
   //       if (key != KEY_LEFT_ALT && key != KEY_RIGHT_ALT)
   //          printk("KB: PRESSED key 0x%x\n", key);
}

static void key_int_handler(u32 key, bool kb_is_pressed)
{
   bool e0 = (key >> 8) == 0xE0;
   key_pressed_state[e0][key & 0xFF] = kb_is_pressed;

   if (kb_is_pressed) {
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
         key_int_handler(scancode & ~0x80u, !(scancode & 0x80));
   }
}

static void kb_process_scancode(u8 scancode)
{
   bool kb_is_pressed;

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

         kb_is_pressed = !(scancode & 0x80);
         scancode &= (u8) ~0x80;

         key_int_handler(scancode | (0xE0u << 8u), kb_is_pressed);
         break;

      case KB_DEFAULT_STATE:
         kb_handle_default_state(scancode);
         break;
   }
}

static enum irq_action keyboard_irq_handler(regs *context)
{
   int count = 0;

   ASSERT(are_interrupts_enabled());
   ASSERT(!is_preemption_enabled());

   if (!kb_wait_cmd_fetched())
      panic("KB: fatal error: timeout in kb_wait_cmd_fetched");

   while (kb_ctrl_is_pending_data()) {

      u8 scancode = inb(KB_DATA_PORT);

      if (!enqueue_tasklet1(kb_tasklet_runner, &kb_process_scancode, scancode))
         panic("KB: hit tasklet queue limit");

      count++;
   }

   return count > 0 ? IRQ_REQUIRES_BH : IRQ_FULLY_HANDLED;
}

u8 kb_get_current_modifiers(void)
{
   u8 shift = 1u * kb_is_shift_pressed();
   u8 alt   = 2u * kb_is_alt_pressed();
   u8 ctrl  = 4u * kb_is_ctrl_pressed();

   /*
    * 0 nothing
    * 1 shift
    * 2 alt
    * 3 shift + alt
    * 4 ctrl
    * 5 shift + ctrl
    * 6 alt + ctrl
    * 7 shift + alt + ctrl
    */

   return shift + alt + ctrl;
}

static void create_kb_tasklet_runner(void)
{
   kb_tasklet_runner =
      create_tasklet_thread(1 /* priority */, KB_TASKLETS_QUEUE_SIZE);

   if (kb_tasklet_runner < 0)
      panic("KB: Unable to create a tasklet runner thread for IRQs");
}

/* NOTE: returns 0 if `key` not in [F1 ... F12] */
int kb_get_fn_key_pressed(u32 key)
{
   /*
    * We know that on the PC architecture, in the PS/2 set 1, keys F1-F12 have
    * all a scancode long 1 byte.
    */

   if (key >= 256)
      return 0;

   static const char fn_table[256] =
   {
      [KEY_F1]  =  1,
      [KEY_F2]  =  2,
      [KEY_F3]  =  3,
      [KEY_F4]  =  4,
      [KEY_F5]  =  5,
      [KEY_F6]  =  6,
      [KEY_F7]  =  7,
      [KEY_F8]  =  8,
      [KEY_F9]  =  9,
      [KEY_F10] = 10,
      [KEY_F11] = 11,
      [KEY_F12] = 12,
   };

   return fn_table[(u8) key];
}

static irq_handler_node kb_irq_handler_node = {
   .node = make_list_node(kb_irq_handler_node.node),
   .handler = keyboard_irq_handler,
};

/* This will be executed in a kernel thread */
void init_kb(void)
{
   disable_preemption();

   if (!kb_ctrl_self_test()) {

      printk("Warning: PS/2 controller self-test failed, trying a reset\n");

      if (!kb_ctrl_reset()) {
         printk("Unable to initialize the PS/2 controller");
         create_kb_tasklet_runner();
         return;
      }

      printk("PS/2 controller: reset successful\n");
   }

   kb_led_update();
   kb_set_typematic_byte(0);

   create_kb_tasklet_runner();

   irq_install_handler(X86_PC_KEYBOARD_IRQ, &kb_irq_handler_node);
   enable_preemption();
}
