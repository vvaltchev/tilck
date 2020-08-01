/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/irq.h>
#include <tilck/kernel/sched.h>

#include "i8042.h"

#define KB_ITERS_TIMEOUT   10000

/*
 * The array `sw_port_enabled` determines which ports Tilck wants to keep
 * enabled, not which ports are currently enabled on the controller.
 * The difference is great because temporarely we can disable all the ports,
 * but later we must re-enable only the ones "logically" enabled, as determined
 * by this array.
 */

static bool sw_port_enabled[2] =
{
   true,    /* Primary PS/2 port, keyboard */
   false    /* Secondary PS/2 port, mouse */
};

void i8042_set_sw_port_enabled_state(u8 port, bool enabled)
{
   ASSERT(port <= 1);
   sw_port_enabled[port] = enabled;
}

bool i8042_get_sw_port_enabled_state(u8 port)
{
   ASSERT(port <= 1);
   return sw_port_enabled[port];
}


/* Hack!!! See pic_io_wait() */
static NO_INLINE void kb_io_wait(void)
{
   if (in_hypervisor())
      return;

   for (int i = 0; i < 1000; i++)
      asmVolatile("nop");
}

static bool kb_wait_cmd_fetched(void)
{
   for (int i = 0; !i8042_is_ready_for_cmd(); i++) {

      if (i >= KB_ITERS_TIMEOUT)
         return false;

      kb_io_wait();
   }

   return true;
}

static NODISCARD bool kb_wait_for_data(void)
{
   for (int i = 0; !i8042_has_pending_data(); i++) {

      if (i >= KB_ITERS_TIMEOUT)
         return false;

      kb_io_wait();
   }

   return true;
}

void i8042_drain_any_data(void)
{
   while (i8042_has_pending_data()) {
      i8042_read_data();
      kb_io_wait();
   }
}

void i8042_force_drain_data(void)
{
   for (int i = 0; i < 16; i++)
      i8042_read_data();
}

static NODISCARD bool i8042_send_cmd(u8 cmd)
{
   if (!kb_wait_cmd_fetched())
      return false;

   outb(I8042_CMD_PORT, cmd);

   if (!kb_wait_cmd_fetched())
      return false;

   return true;
}

static NODISCARD bool i8042_send_cmd_and_wait_response(u8 cmd)
{
   if (!i8042_send_cmd(cmd))
      return false;

   if (!kb_wait_for_data())
      return false;

   return true;
}

static NODISCARD bool i8042_full_wait(void)
{
   u8 ctrl;
   u32 iters = 0;

   do
   {
      if (iters > KB_ITERS_TIMEOUT)
         return false;

      ctrl = inb(I8042_STATUS_PORT);

      if (ctrl & I8042_STATUS_OUTPUT_FULL) {
         i8042_read_data(); /* drain the KB's output */
      }

      iters++;
      kb_io_wait();

   } while (ctrl & (I8042_STATUS_INPUT_FULL | I8042_STATUS_OUTPUT_FULL));

   return true;
}

NODISCARD bool i8042_disable_ports(void)
{
   irq_set_mask(X86_PC_KEYBOARD_IRQ);

   if (!i8042_full_wait())
      return false;

   if (!i8042_send_cmd(I8042_CMD_PORT1_DISABLE))
      return false;

   if (!i8042_send_cmd(I8042_CMD_PORT2_DISABLE))
      return false;

   if (!i8042_full_wait())
      return false;

   return true;
}

NODISCARD bool i8042_enable_ports(void)
{
   if (!i8042_full_wait())
      return false;

   if (sw_port_enabled[0])
      if (!i8042_send_cmd(I8042_CMD_PORT1_ENABLE))
         return false;

   if (sw_port_enabled[1])
      if (!i8042_send_cmd(I8042_CMD_PORT2_ENABLE))
         return false;

   if (!i8042_full_wait())
      return false;

   i8042_force_drain_data();
   irq_clear_mask(X86_PC_KEYBOARD_IRQ);
   return true;
}

bool kb_led_set(u8 val)
{
   if (!i8042_disable_ports()) {
      printk("KB: i8042_disable_ports() fail\n");
      return false;
   }

   if (!i8042_full_wait()) goto err;
   outb(I8042_DATA_PORT, 0xED);
   if (!i8042_full_wait()) goto err;
   outb(I8042_DATA_PORT, val & 7);
   if (!i8042_full_wait()) goto err;

   if (!i8042_enable_ports()) {
      printk("KB: i8042_enable_ports() fail\n");
      return false;
   }

   return true;

err:
   printk("kb_led_set() failed: timeout in i8042_full_wait()\n");
   return false;
}

/*
 * From http://wiki.osdev.org/PS/2_Keyboard
 *
 * BITS [0..4]: Repeat rate (00000b = 30 Hz, ..., 11111b = 2 Hz)
 * BITS [5..6]: Delay before keys repeat (00b = 250 ms, ..., 11b = 1000 ms)
 * BIT  [7]: Must be zero
 *
 * Note: this function sets just the repeat rate.
 */

bool kb_set_typematic_byte(u8 val)
{
   if (!i8042_disable_ports()) {
      printk("kb_set_typematic_byte() failed: i8042_disable_ports() fail\n");
      return false;
   }

   if (!i8042_full_wait()) goto err;
   outb(I8042_DATA_PORT, 0xF3);
   if (!i8042_full_wait()) goto err;
   outb(I8042_DATA_PORT, val & 0b11111);
   if (!i8042_full_wait()) goto err;

   if (!i8042_enable_ports()) {
      printk("kb_set_typematic_byte() failed: i8042_enable_ports() fail\n");
      return false;
   }

   return true;

err:
   printk("kb_set_typematic_byte() failed: timeout in i8042_full_wait()\n");
   return false;
}

NODISCARD bool i8042_self_test(void)
{
   u8 res, resend_count = 0;
   bool success = false;

   if (!i8042_disable_ports())
      goto out;

   do {

      if (resend_count >= 3)
         break;

      if (!i8042_send_cmd_and_wait_response(I8042_CMD_SELFTEST))
         goto out;

      res = i8042_read_data();
      resend_count++;

   } while (res == KB_RESPONSE_RESEND);

   if (res == KB_RESPONSE_SELF_TEST_OK)
      success = true;

out:
   if (!i8042_enable_ports())
      success = false;

   return success;
}

NODISCARD bool i8042_reset(void)
{
   u8 res;
   u8 kb_ctrl;
   u8 resend_count = 0;
   bool success = false;

   if (!i8042_disable_ports())
      goto out;

   kb_ctrl = inb(I8042_STATUS_PORT);

   printk("KB: reset procedure\n");
   printk("KB: initial status: 0x%x\n", kb_ctrl);
   printk("KB: sending 0xFF (reset) to the controller\n");

   if (!i8042_send_cmd_and_wait_response(I8042_CMD_RESET))
      goto out;

   do {

      if (resend_count >= 3)
         break;

      res = i8042_read_data();
      printk("KB: response: 0x%x\n", res);
      resend_count++;

   } while (res == KB_RESPONSE_RESEND);

   if (res != KB_RESPONSE_ACK) {

      if (res == KB_RESPONSE_BAT_OK)
         success = true;

      goto out;
   }

   /* We got an ACK, now wait for the success/failure of the reset itself */

   if (!kb_wait_for_data())
      goto out;

   res = i8042_read_data();
   printk("KB: response: 0x%x\n", res);

   if (res == KB_RESPONSE_BAT_OK)
      success = true;

out:
   if (!i8042_enable_ports())
      success = false;

   printk("KB: reset success: %u\n", success);
   return success;
}

bool i8042_read_ctr_unsafe(u8 *ctr)
{
   if (!i8042_send_cmd_and_wait_response(I8042_CMD_READ_CTR)) {
      printk("KB: send cmd failed\n");
      return false;
   }

   *ctr = i8042_read_data();
   return true;
}

bool i8042_read_cto_unsafe(u8 *cto)
{
   if (!i8042_send_cmd_and_wait_response(I8042_CMD_READ_CTO)) {
      printk("KB: send cmd failed\n");
      return false;
   }

   *cto = i8042_read_data();
   return true;
}

bool i8042_read_regs(u8 *ctr, u8 *cto)
{
   bool ok = true;
   ASSERT(are_interrupts_enabled());

   if (!i8042_disable_ports()) {
      printk("KB: disable ports failed\n");
      ok = false;
   }

   if (ctr && ok)
      ok = i8042_read_ctr_unsafe(ctr);

   if (cto && ok)
      ok = i8042_read_cto_unsafe(cto);

   if (!i8042_enable_ports()) {
      printk("KB: enable ports failed\n");
      ok = false;
   }

   return ok;
}

// Reboot procedure using the 8042 PS/2 controller
void i8042_reboot(void)
{
   disable_interrupts_forced(); /* Disable the interrupts before rebooting */

   if (!i8042_send_cmd(I8042_CMD_CPU_RESET))
      panic("Unable to reboot using the 8042 controller: timeout in send cmd");

   while (true) {
      halt();
   }
}
