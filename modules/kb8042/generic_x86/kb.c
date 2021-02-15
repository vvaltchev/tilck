/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_kb8042.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/modules.h>
#include <tilck/kernel/worker_thread.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/safe_ringbuf.h>

#include <tilck/mods/acpi.h>

#include "i8042.h"
#include "kb_layouts.c.h"
#include "kb_ansi_seq.c.h"

enum kb_state {

   KB_DEFAULT_STATE = 0,
   KB_READ_E0_SCANCODE_STATE,
   KB_READ_E1_SCANCODE_STATE,
   KB_READ_FIRST_SCANCODE_AFTER_E1_STATE,
};

static struct worker_thread *kb_worker_thread;
static enum kb_state kb_curr_state;
static bool key_pressed_state[2][128];
static bool numLock;
static bool capsLock;
static struct list keypress_handlers = STATIC_LIST_INIT(keypress_handlers);
static struct kb_dev ps2_keyboard;
static struct safe_ringbuf kb_input_rb;

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

static bool handle_special_keys_pressed(u32 key)
{
   switch (key) {

      case KEY_DEL:

         if (kb_is_pressed(KEY_LEFT_CTRL) && kb_is_pressed(KEY_LEFT_ALT)) {
            printk("Ctrl + Alt + Del: Reboot!\n");
            reboot();
            NOT_REACHED();
         }

         break;

      case KEY_NUM_LOCK:
         numLock = !numLock;
         kb_led_update();
         return true;

      case KEY_CAPS_LOCK:
         capsLock = !capsLock;
         kb_led_update();
         return true;
   }

   return false;
}

void handle_key_pressed(u32 key, bool pressed)
{
   if (pressed)
      if (handle_special_keys_pressed(key))
         return;

   kb_call_keypress_handlers(
      make_key_event(key, translate_printable_key(key), pressed)
   );
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

static void kb_dump_regs(u8 ctr, u8 cto, u8 status)
{
   bool masked = irq_is_masked(X86_PC_KEYBOARD_IRQ);
   printk("KB: IRQ masked:                   %u\n", masked);
   printk("KB: Ctrl Config. Byte (CTR):   0x%02x\n", ctr);
   printk("KB: Ctrl Output Port (CTO):    0x%02x\n", cto);
   printk("KB: Status register:           0x%02x\n", status);
}

static void kb_irq_bottom_half(void *arg)
{
   u8 scancode;
   disable_preemption();
   {
      while (safe_ringbuf_read_1(&kb_input_rb, &scancode)) {
         kb_process_scancode(scancode);
      }
   }
   enable_preemption();
}

static int kb_irq_handler_read_scancodes(void)
{
   int count = 0;

   while (i8042_has_pending_data()) {

      u8 scancode = i8042_read_data();
      bool was_empty;

      if (!safe_ringbuf_write_1(&kb_input_rb, &scancode, &was_empty)) {
         /* We have no other choice than to just drain the data */
         printk("KB: Warning: hit input limit. Drain the data!\n");
         i8042_drain_any_data();
      }

      count++;
   }

   return count;
}

static enum irq_action keyboard_irq_handler(void *ctx)
{
   ASSERT(are_interrupts_enabled());
   ASSERT(!is_preemption_enabled());

   if (!i8042_is_ready_for_cmd()) {
      printk("KB: Warning: got IRQ with pending command\n");
      return IRQ_HANDLED;
   }

   if (!kb_irq_handler_read_scancodes()) {

      /* Got IRQ *without* output buffer full set in the status register */

      if (PS2_VERBOSE_DEBUG_LOG) {
         printk("KB: Got IRQ#1 with OBF=0 in status register\n");
      }

      i8042_force_drain_data();
      return IRQ_HANDLED;
   }

   /* Everything is fine: we read at least one scancode */
   if (!wth_enqueue_on(kb_worker_thread, &kb_irq_bottom_half, NULL)) {

      /*
       * While on real hardware this should NEVER happen, on some slow emulators
       * that's totally possible. For example, on this pure browser emulator:
       * https://copy.sh/v86/
       *
       * If we scroll fast up and down immediately after boot while the
       * fb_pre_render_char_scanlines() function has not completed yet,
       * the scroll will be so slow that sometimes the queue of tasks for this
       * bottom half will fill up.
       */

      printk("WARNING: KB: unable to enqueue job\n");
   }

   return IRQ_HANDLED;
}

static u8 kb_translate_to_mediumraw(struct key_event ke)
{
   const u32 key = ke.key;

   if ((key & 0xff) == key)
      return (u8)(key | (u8)(!ke.pressed << 7));

   return mediumraw_e0_keys[key & 0xff] | (u8)(!ke.pressed << 7);
}

static void create_kb_worker_thread(void)
{
   u8 *kb_input_buf = kmalloc(512);

   if (!kb_input_buf)
      panic("KB: unable to alloc kb_input_buf");

   safe_ringbuf_init(&kb_input_rb, 512, 1, kb_input_buf);

   kb_worker_thread =
      wth_create_thread("kb", 1 /* priority */, WTH_KB_QUEUE_SIZE);

   if (!kb_worker_thread)
      panic("KB: Unable to create a worker thread for IRQs");
}

static struct kb_dev ps2_keyboard = {

   .driver_name = "ps2",
   .is_pressed = kb_is_pressed,
   .register_handler = kb_register_keypress_handler,
   .scancode_to_ansi_seq = kb_scancode_to_ansi_seq,
   .translate_to_mediumraw = kb_translate_to_mediumraw,
};

DEFINE_IRQ_HANDLER_NODE(keyboard, keyboard_irq_handler, &ps2_keyboard);

static bool hw_8042_init_first_steps(void)
{
   u8 status, ctr = 0, cto = 0;
   bool dump_regs = PS2_VERBOSE_DEBUG_LOG;

   status = i8042_read_status();

   if (!i8042_read_ctr_unsafe(&ctr)) {
      printk("KB: read CTR failed\n");
      return false;
   }

   if (!in_hypervisor() && !(ctr & I8042_CTR_SYS_FLAG)) {
      printk("KB: Warning: unset system flag in CTR\n");
      dump_regs = true;
   }

   if (dump_regs) {

      if (!i8042_read_cto_unsafe(&cto)) {
         printk("KB: read CTO failed\n");
         return false;
      }

      kb_dump_regs(ctr, cto, status);
   }

   return true;
}

static bool hw_8042_init(void)
{
   bool ok;
   ASSERT(!is_preemption_enabled());

   if (!i8042_disable_ports())
      return false;

   ok = hw_8042_init_first_steps();

   if (!i8042_enable_ports())
      return false;

   if (!ok)
      return false;

   if (!PS2_DO_SELFTEST)
      return true;

   if (i8042_self_test())
      return true;

   printk("KB: PS/2 controller self-test failed, trying a reset\n");

   if (i8042_reset()) {
      printk("KB: PS/2 controller: reset successful\n");
      return true;
   }

   printk("KB: Unable to initialize the PS/2 controller\n");
   return false;
}

static void
init_kb_internal(void)
{
   const bool acpi_ok = get_acpi_init_status() == ais_fully_initialized;

   /*
    * While it would be great to not even try initializing the i8042 controller
    * if ACPI says it's not present on the system, apparently that's not a good
    * idea because the value of the ACPI_FADT_8042 flag in FADT's IAPC_BOOT_ARCH
    * is not reliable. E.g.: I have a (pure UEFI) machine that has FADT v6 which
    * has ACPI_FADT_8042 unset, even if a i8042 controller exists and Linux
    * confirms that.
    *
      if (MOD_acpi && acpi_ok) {
         if (acpi_is_8042_present() == tri_no) {
            printk("KB: no 8042 controller (PS/2) detected\n");
            return;
         }
      }
    */


   if (!hw_8042_init()) {

      if (acpi_ok)
         printk("KB: no 8042 controller (PS/2) detected\n");
      else
         printk("KB: hw_8042_init() failed\n");

      return;
   }

   if (in_hypervisor()) {

      /*
       * In case of real HW, we can assume numLock is off on boot, while
       * when running inside a VM, the numLock can still be off in the VM
       * itself while being (typically) turned on in the host. Because we
       * cannot control the `numLock` state in the host and we're not even
       * guarateed to be able to catch the `numLock` key press, assuming it as
       * turned on when running in a VM, is typically the best choice.
       */
      numLock = true;
   }

   kb_led_update();
   kb_set_typematic_byte(0);

   create_kb_worker_thread();
   irq_install_handler(X86_PC_KEYBOARD_IRQ, &keyboard);
   register_keyboard_device(&ps2_keyboard);
}

/* This will be executed in a kernel thread */
void init_kb(void)
{
   if (kopt_serial_console)
      return;

   disable_preemption();
   {
      init_kb_internal();
   }
   enable_preemption();
}

static struct module kb_ps2_module = {

   .name = "kb8042",
   .priority = MOD_kb_prio,
   .init = &init_kb,
};

REGISTER_MODULE(&kb_ps2_module);
