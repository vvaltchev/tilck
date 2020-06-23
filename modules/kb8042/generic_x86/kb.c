/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/modules.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/sched.h>

#include "kb_int.c.h"
#include "kb_layouts.c.h"
#include "kb_ansi_seq.c.h"

enum kb_state {

   KB_DEFAULT_STATE = 0,
   KB_READ_E0_SCANCODE_STATE,
   KB_READ_E1_SCANCODE_STATE,
   KB_READ_FIRST_SCANCODE_AFTER_E1_STATE,
};

int kb_tasklet_runner = -1;
static enum kb_state kb_curr_state;
static bool key_pressed_state[2][128];
static bool numLock = true;
static bool capsLock = false;
static struct list keypress_handlers = make_list(keypress_handlers);
static struct kb_dev ps2_keyboard;

static bool kb_is_pressed(u32 key)
{
   bool e0 = (key >> 8) == 0xE0;
   return key_pressed_state[e0][key & 0xFF];
}

static inline void kb_led_update(void)
{
   kb_led_set((u8)(capsLock << 2 | numLock << 1));
}

static char translate_printable_key(u32 key)
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

   char *layout =
      us_kb_layouts[kb_is_pressed(KEY_L_SHIFT) || kb_is_pressed(KEY_R_SHIFT)];

   char c = layout[key];

   if (numLock)
      c |= numkey[key];

   if (capsLock)
      c = (char)toupper(c);

   return c;
}

static void kb_register_keypress_handler(struct keypress_handler_elem *e)
{
   list_add_tail(&keypress_handlers, &e->node);
}

static int kb_call_keypress_handlers(struct key_event ke)
{
   int count = 0;
   struct keypress_handler_elem *pos;

   list_for_each_ro(pos, &keypress_handlers, node) {

      enum kb_handler_action a = pos->handler(&ps2_keyboard, ke);

      switch (a) {
         case kb_handler_ok_and_stop:
            count++;
            return count;

         case kb_handler_ok_and_continue:
            count++;
            break;

         case kb_handler_nak:
            break;

         default:
            NOT_REACHED();
      }
   }

   return count;
}

void handle_key_pressed(u32 key, bool pressed)
{
   switch (key) {

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
   kb_call_keypress_handlers(
      make_key_event(key, translate_printable_key(key), pressed)
   );

   // if (!hc && key != KEY_L_SHIFT && key != KEY_R_SHIFT)
   //    if (key != KEY_LEFT_CTRL && key != KEY_RIGHT_CTRL)
   //       if (key != KEY_LEFT_ALT && key != KEY_RIGHT_ALT)
   //          printk("KB: PRESSED key 0x%x\n", key);
}

static void key_int_handler(u32 key, bool kb_is_pressed)
{
   bool e0 = (key >> 8) == 0xE0;
   key_pressed_state[e0][key & 0xFF] = kb_is_pressed;
   handle_key_pressed(key, kb_is_pressed);
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

static enum irq_action keyboard_irq_handler(regs_t *context)
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

static u8 kb_translate_to_mediumraw(struct key_event ke)
{
   const u32 key = ke.key;

   if ((key & 0xff) == key)
      return (u8)(key | (u8)(!ke.pressed << 7));

   return mediumraw_e0_keys[key & 0xff] | (u8)(!ke.pressed << 7);
}

static void create_kb_tasklet_runner(void)
{
   kb_tasklet_runner =
      create_tasklet_thread(1 /* priority */, KB_TASKLETS_QUEUE_SIZE);

   if (kb_tasklet_runner < 0)
      panic("KB: Unable to create a tasklet runner thread for IRQs");
}

static struct irq_handler_node kb_irq_handler_node = {
   .node = make_list_node(kb_irq_handler_node.node),
   .handler = keyboard_irq_handler,
};

static struct kb_dev ps2_keyboard = {

   .driver_name = "ps2",
   .is_pressed = kb_is_pressed,
   .register_handler = kb_register_keypress_handler,
   .scancode_to_ansi_seq = kb_scancode_to_ansi_seq,
   .translate_to_mediumraw = kb_translate_to_mediumraw,
};

/* This will be executed in a kernel thread */
void init_kb(void)
{
   if (kopt_serial_console)
      return;

   disable_preemption();

   if (KERNEL_DO_PS2_SELFTEST) {
      if (!kb_ctrl_self_test()) {

         printk("Warning: PS/2 controller self-test failed, trying a reset\n");

         if (!kb_ctrl_reset()) {
            printk("Unable to initialize the PS/2 controller");
            create_kb_tasklet_runner();
            return;
         }

         printk("PS/2 controller: reset successful\n");
      }
   }


   kb_led_update();
   kb_set_typematic_byte(0);

   create_kb_tasklet_runner();
   irq_install_handler(X86_PC_KEYBOARD_IRQ, &kb_irq_handler_node);

   register_keyboard_device(&ps2_keyboard);
   enable_preemption();
}

static struct module kb_ps2_module = {

   .name = "kbps2",
   .priority = 50,
   .init = &init_kb,
};

REGISTER_MODULE(&kb_ps2_module);
