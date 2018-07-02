

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

static bool pkeys[128];
static bool e0pkeys[128];
static bool *pkeysArrays[2] = { pkeys, e0pkeys };

static bool numLock = false;
static bool capsLock = false;
static bool lastWasE0 = false;

static u8 next_scancodes_to_ignore = 0; // HACK to skip 0xE1 sequences

static void numlock_set_led(bool val)
{
   kb_led_set(capsLock << 2 | val << 1);
}

static void capslock_set_led(bool val)
{
   kb_led_set(numLock << 1 | val << 2);
}

/*
 * Condition variable on which tasks interested in keyboard input, wait.
 */
kcond kb_cond;

void print_slow_timer_irq_handler_counter(void);
void debug_term_print_scroll_cycles(void);
extern u32 spur_irq_count;

void debug_show_spurious_irq_count(void)
{
#if KERNEL_TRACK_NESTED_INTERRUPTS
      print_slow_timer_irq_handler_counter();
#endif

   if (get_ticks() > TIMER_HZ)
      printk("Spur IRQ count: %u (%u/sec)\n",
               spur_irq_count,
               spur_irq_count / (get_ticks() / TIMER_HZ));
   else
      printk("Spurious IRQ count: %u (< 1 sec)\n",
               spur_irq_count, spur_irq_count);
}

void handle_key_pressed(u8 scancode)
{
   switch(scancode) {

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
            kcond_signal_one(&kb_cond);
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

u64 kb_press_start = 0;

void kb_tasklet_handler(u8 scancode)
{
   // u64 cycles = RDTSC() - kb_press_start;
   // printk("latency: %llu cycles\n", cycles);

   // Hack used to avoid handling 0xE1 two-scancode sequences
   if (next_scancodes_to_ignore) {
      next_scancodes_to_ignore--;
      return;
   }

   // Hack used to avoid handling 0xE1 two-scancode sequences
   if (scancode == 0xE1) {
      next_scancodes_to_ignore = 2;
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

   kb_press_start = RDTSC();
   VERIFY(enqueue_tasklet1(&kb_tasklet_handler, scancode));
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

   irq_install_handler(X86_PC_KEYBOARD_IRQ, keyboard_irq_handler);
   enable_preemption();

   printk("keyboard initialized.\n");
}
