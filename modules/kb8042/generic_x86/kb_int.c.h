/* SPDX-License-Identifier: BSD-2-Clause */

#define KB_ITERS_TIMEOUT   10000

#define KB_DATA_PORT        0x60       /* 0x60 is a read/write data port */
#define KB_COMMAND_PORT     0x64       /* 0x64 is for cmds, when it's written */
#define KB_STATUS_PORT      0x64       /* 0x64 is for status, when it's read */

/* Status flags */

/* The output buffer is full: it must be 1 before reading from KB_DATA_PORT */
#define KB_STATUS_OUTPUT_FULL          (1 << 0)

/*
 * The input buffer is full: it must be 0 before writing data to KB_DATA_PORT or
 * KB_COMMAND_PORT.
 */
#define KB_STATUS_INPUT_FULL           (1 << 1)

/* CTR (Controller Configuration Byte) flags */
#define KB_CTR_PORT1_INT_ENABLED       (1 << 0)
#define KB_CTR_PORT2_INT_ENABLED       (1 << 1)
#define KB_CTR_SYS_FLAG                (1 << 2)
#define KB_CTR_PORT1_CLOCK_DISABLED    (1 << 4)
#define KB_CTR_PORT2_CLOCK_DISABLED    (1 << 5)
#define KB_CTR_PS2_PORT_TRANSLATION    (1 << 6)

/* PS/2 Controller commands */
#define KB_CTRL_CMD_CPU_RESET              0xFE
#define KB_CTRL_CMD_RESET                  0xFF
#define KB_CTRL_CMD_SELFTEST               0xAA
#define KB_CTRL_CMD_PORT1_DISABLE          0xAD
#define KB_CTRL_CMD_PORT1_ENABLE           0xAE
#define KB_CTRL_CMD_PORT2_DISABLE          0xA7
#define KB_CTRL_CMD_PORT2_ENABLE           0xA8
#define KB_CTRL_CMD_READ_CTR               0x20 /* Controller Config. Byte */
#define KB_CTRL_CMD_READ_CTO               0xD0 /* Controller Output Port */

/* Response codes */
#define KB_RESPONSE_ACK                    0xFA
#define KB_RESPONSE_RESEND                 0xFE
#define KB_RESPONSE_BAT_OK                 0xAA
#define KB_RESPONSE_SELF_TEST_OK           0x55

/* Hack!!! See pic_io_wait() */
static NO_INLINE void kb_io_wait(void)
{
   if (in_hypervisor())
      return;

   for (int i = 0; i < 1000; i++)
      asmVolatile("nop");
}

static ALWAYS_INLINE NODISCARD bool kb_ctrl_is_pending_data(void)
{
   return !!(inb(KB_STATUS_PORT) & KB_STATUS_OUTPUT_FULL);
}

static ALWAYS_INLINE NODISCARD bool kb_ctrl_is_read_for_next_cmd(void)
{
   return !(inb(KB_STATUS_PORT) & KB_STATUS_INPUT_FULL);
}

static bool kb_wait_cmd_fetched(void)
{
   for (int i = 0; !kb_ctrl_is_read_for_next_cmd(); i++) {

      if (i >= KB_ITERS_TIMEOUT)
         return false;

      kb_io_wait();
   }

   return true;
}

static NODISCARD bool kb_wait_for_data(void)
{
   for (int i = 0; !kb_ctrl_is_pending_data(); i++) {

      if (i >= KB_ITERS_TIMEOUT)
         return false;

      kb_io_wait();
   }

   return true;
}

static void kb_drain_any_data(void)
{
   while (kb_ctrl_is_pending_data()) {
      inb(KB_DATA_PORT);
      kb_io_wait();
   }
}

static void kb_drain_data_no_check(void)
{
   for (int i = 0; i < 16; i++)
      inb(KB_DATA_PORT);
}

static NODISCARD bool kb_ctrl_send_cmd(u8 cmd)
{
   if (!kb_wait_cmd_fetched())
      return false;

   outb(KB_COMMAND_PORT, cmd);

   if (!kb_wait_cmd_fetched())
      return false;

   return true;
}

static NODISCARD bool kb_ctrl_send_cmd_and_wait_response(u8 cmd)
{
   if (!kb_ctrl_send_cmd(cmd))
      return false;

   if (!kb_wait_for_data())
      return false;

   return true;
}

static NODISCARD bool kb_ctrl_full_wait(void)
{
   u8 ctrl;
   u32 iters = 0;

   do
   {
      if (iters > KB_ITERS_TIMEOUT)
         return false;

      ctrl = inb(KB_STATUS_PORT);

      if (ctrl & KB_STATUS_OUTPUT_FULL) {
         inb(KB_DATA_PORT); /* drain the KB's output */
      }

      iters++;
      kb_io_wait();

   } while (ctrl & (KB_STATUS_INPUT_FULL | KB_STATUS_OUTPUT_FULL));

   return true;
}

static NODISCARD bool kb_ctrl_disable_ports(void)
{
   irq_set_mask(X86_PC_KEYBOARD_IRQ);

   if (!kb_ctrl_full_wait())
      return false;

   if (!kb_ctrl_send_cmd(KB_CTRL_CMD_PORT1_DISABLE))
      return false;

   if (!kb_ctrl_send_cmd(KB_CTRL_CMD_PORT2_DISABLE))
      return false;

   if (!kb_ctrl_full_wait())
      return false;

   return true;
}

static NODISCARD bool kb_ctrl_enable_ports(void)
{
   if (!kb_ctrl_full_wait())
      return false;

   if (!kb_ctrl_send_cmd(KB_CTRL_CMD_PORT1_ENABLE))
      return false;

   if (!kb_ctrl_send_cmd(KB_CTRL_CMD_PORT2_ENABLE))
      return false;

   if (!kb_ctrl_full_wait())
      return false;

   kb_drain_data_no_check();
   irq_clear_mask(X86_PC_KEYBOARD_IRQ);
   return true;
}

bool kb_led_set(u8 val)
{
   if (!kb_ctrl_disable_ports()) {
      printk("KB: kb_ctrl_disable_ports() fail\n");
      return false;
   }

   if (!kb_ctrl_full_wait()) goto err;
   outb(KB_DATA_PORT, 0xED);
   if (!kb_ctrl_full_wait()) goto err;
   outb(KB_DATA_PORT, val & 7);
   if (!kb_ctrl_full_wait()) goto err;

   if (!kb_ctrl_enable_ports()) {
      printk("KB: kb_ctrl_enable_ports() fail\n");
      return false;
   }

   return true;

err:
   printk("kb_led_set() failed: timeout in kb_ctrl_full_wait()\n");
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
   if (!kb_ctrl_disable_ports()) {
      printk("kb_set_typematic_byte() failed: kb_ctrl_disable_ports() fail\n");
      return false;
   }

   if (!kb_ctrl_full_wait()) goto err;
   outb(KB_DATA_PORT, 0xF3);
   if (!kb_ctrl_full_wait()) goto err;
   outb(KB_DATA_PORT, val & 0b11111);
   if (!kb_ctrl_full_wait()) goto err;

   if (!kb_ctrl_enable_ports()) {
      printk("kb_set_typematic_byte() failed: kb_ctrl_enable_ports() fail\n");
      return false;
   }

   return true;

err:
   printk("kb_set_typematic_byte() failed: timeout in kb_ctrl_full_wait()\n");
   return false;
}

static NODISCARD bool kb_ctrl_self_test(void)
{
   u8 res, resend_count = 0;
   bool success = false;

   if (!kb_ctrl_disable_ports())
      goto out;

   do {

      if (resend_count >= 3)
         break;

      if (!kb_ctrl_send_cmd_and_wait_response(KB_CTRL_CMD_SELFTEST))
         goto out;

      res = inb(KB_DATA_PORT);
      resend_count++;

   } while (res == KB_RESPONSE_RESEND);

   if (res == KB_RESPONSE_SELF_TEST_OK)
      success = true;

out:
   if (!kb_ctrl_enable_ports())
      success = false;

   return success;
}

static NODISCARD bool kb_ctrl_reset(void)
{
   u8 res;
   u8 kb_ctrl;
   u8 resend_count = 0;
   bool success = false;

   if (!kb_ctrl_disable_ports())
      goto out;

   kb_ctrl = inb(KB_STATUS_PORT);

   printk("KB: reset procedure\n");
   printk("KB: initial status: 0x%x\n", kb_ctrl);
   printk("KB: sending 0xFF (reset) to the controller\n");

   if (!kb_ctrl_send_cmd_and_wait_response(KB_CTRL_CMD_RESET))
      goto out;

   do {

      if (resend_count >= 3)
         break;

      res = inb(KB_DATA_PORT);
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

   res = inb(KB_DATA_PORT);
   printk("KB: response: 0x%x\n", res);

   if (res == KB_RESPONSE_BAT_OK)
      success = true;

out:
   if (!kb_ctrl_enable_ports())
      success = false;

   printk("KB: reset success: %u\n", success);
   return success;
}

static u8 kb_ctrl_read_status(void)
{
   return inb(KB_STATUS_PORT);
}

static bool kb_ctrl_read_ctr_and_cto(u8 *ctr, u8 *cto)
{
   bool ok = true;
   ASSERT(are_interrupts_enabled());

   if (!kb_ctrl_disable_ports()) {
      printk("KB: disable ports failed\n");
      ok = false;
      goto out;
   }

   if (ctr) {
      if (kb_ctrl_send_cmd_and_wait_response(KB_CTRL_CMD_READ_CTR)) {
         *ctr = inb(KB_DATA_PORT);
      } else {
         printk("KB: send cmd failed\n");
         ok = false;
      }
   }

   if (cto) {
      if (kb_ctrl_send_cmd_and_wait_response(KB_CTRL_CMD_READ_CTO)) {
         *cto = inb(KB_DATA_PORT);
      } else {
         printk("KB: send cmd failed\n");
         ok = false;
      }
   }

out:
   if (!kb_ctrl_enable_ports()) {
      printk("KB: enable ports failed\n");
      ok = false;
   }

   return ok;
}

// Reboot procedure using the 8042 PS/2 controller
void x86_pc_8042_reboot(void)
{
   disable_interrupts_forced(); /* Disable the interrupts before rebooting */

   if (!kb_ctrl_send_cmd(KB_CTRL_CMD_CPU_RESET))
      panic("Unable to reboot using the 8042 controller: timeout in send cmd");

   while (true) {
      halt();
   }
}
