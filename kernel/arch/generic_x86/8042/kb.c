
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>

#include <exos/kernel/tasklet.h>
#include <exos/kernel/process.h>
#include <exos/kernel/list.h>
#include <exos/kernel/kb.h>
#include <exos/kernel/errno.h>
#include <exos/kernel/kb_scancode_set1_keys.h>

#include "kb_int.c.h"
#include "kb_layouts.c.h"

#define KB_TASKLETS_QUEUE_SIZE 128

typedef enum {

   KB_DEFAULT_STATE,
   KB_READ_E0_SCANCODE_STATE,
   KB_READ_E1_SCANCODE_STATE,
   KB_READ_FIRST_SCANCODE_AFTER_E1_STATE

} kb_state_t;

typedef struct {

   list_node list;
   keypress_func handler;

} keypress_handler_elem;

static kb_state_t kb_curr_state = KB_DEFAULT_STATE;
static int kb_tasklet_runner = -1;
static bool key_pressed_state[2][128];
static bool numLock;
static bool capsLock;
static list_node keypress_handlers;

bool kb_is_pressed(u32 key)
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
   if (key >= 256)
      return 0;

   u8 *layout =
      us_kb_layouts[kb_is_pressed(KEY_L_SHIFT) || kb_is_pressed(KEY_R_SHIFT)];

   u8 c = layout[key];

   if (numLock)
      c |= numkey[key];

   if (capsLock)
      c = toupper(c);

   return c;
}

int kb_register_keypress_handler(keypress_func f)
{
   keypress_handler_elem *e = kmalloc(sizeof(keypress_handler_elem));

   if (!e)
      return -ENOMEM;

   list_node_init(&e->list);
   e->handler = f;

   list_add_tail(&keypress_handlers, &e->list);
   return 0;
}

static int kb_call_keypress_handlers(u32 raw_key, u8 printable_char)
{
   int count = 0;
   keypress_handler_elem *pos;

   list_for_each(pos, &keypress_handlers, list) {

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

   case KEY_E0_DEL:

      if (kb_is_pressed(KEY_CTRL) && kb_is_pressed(KEY_ALT)) {
         printk("Ctrl + Alt + Del: Reboot!\n");
         reboot();
      }

      break;

   case KEY_NUM_LOCK:
      numLock = !numLock;
      numlock_set_led(numLock);
      //printk("\nNUM LOCK is %s\n", numLock ? "ON" : "OFF");
      return;

   case KEY_CAPS_LOCK:
      capsLock = !capsLock;
      capslock_set_led(capsLock);
      //printk("\nCAPS LOCK is %s\n", capsLock ? "ON" : "OFF");
      return;
   }

   int hc = kb_call_keypress_handlers(key, translate_printable_key(key));

   if (!hc && key != KEY_L_SHIFT && key != KEY_R_SHIFT) {
      printk("KB: PRESSED key 0x%x\n", key);
   }
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
         key_int_handler(scancode & ~0x80, !(scancode & 0x80));
   }
}

static void kb_tasklet_handler(u8 scancode)
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
         scancode &= ~0x80;

         key_int_handler(scancode | (0xE0 << 8), kb_is_pressed);
         break;

      case KB_DEFAULT_STATE:
         kb_handle_default_state(scancode);
         break;
   }
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

   list_node_init(&keypress_handlers);

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
