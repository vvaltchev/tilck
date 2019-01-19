/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/serial.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/cmdline.h>

extern int kb_tasklet_runner;

static void com1_handler_func(char c)
{
   tty_keypress_handler_int((u32)c, c, false);
}

int serial_com1_irq_handler(regs *r)
{
   char c;

   if (!serial_read_ready(COM1))
      return 0;

   while (serial_read_ready(COM1)) {

      c = serial_read(COM1);

      if (!enqueue_tasklet1(kb_tasklet_runner, &com1_handler_func, c))
         panic("KB: hit tasklet queue limit");
   }

   return 0;
}

int serial_com2_irq_handler(regs *r)
{
   int c = 0;

   if (!serial_read_ready(COM2))
      return 0;

   while (serial_read_ready(COM2)) {
      serial_read(COM2);
      c++;
   }

   printk("COM2: irq. Read count: %d\n", c);
   return 0;
}

void early_init_serial_ports(void)
{
   init_serial_port(COM1);
   init_serial_port(COM2);
}

void init_serial_comm(void)
{
   if (kopt_serial_mode == TERM_SERIAL_CONSOLE)
      irq_install_handler(X86_PC_COM1_IRQ, &serial_com1_irq_handler);

   irq_install_handler(X86_PC_COM2_IRQ, &serial_com2_irq_handler);
}
