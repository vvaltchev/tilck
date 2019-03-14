/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/serial.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/cmdline.h>
#include "tty/tty_int.h"

/* NOTE: hw-specific stuff in generic code. TODO: fix that. */
static const u16 com_ports[] = {COM1, COM2, COM3, COM4};
static int serial_port_tasklet_runner;

static void serial_con_bh_handler(u16 portn)
{
   while (serial_read_ready(com_ports[portn])) {

      u8 c = (u8)serial_read(com_ports[portn]);
      tty_keypress_handler_int(ttys[64+portn], c, c, false);
   }
}

static int serial_con_irq_handler(regs *r, u16 portn)
{
   if (!serial_read_ready(com_ports[portn]))
      return 0;

   if (!enqueue_tasklet1(serial_port_tasklet_runner,
                         &serial_con_bh_handler, portn))
   {
      panic("KB: hit tasklet queue limit");
   }

   return 1;
}

static int serial_com1_irq_handler(regs *r)
{
   return serial_con_irq_handler(r, 0);
}

static int serial_com2_irq_handler(regs *r)
{
   return serial_con_irq_handler(r, 1);
}

void early_init_serial_ports(void)
{
   init_serial_port(COM1);
   init_serial_port(COM2);
}

void init_serial_comm(void)
{
   serial_port_tasklet_runner =
      create_tasklet_thread(1 /* priority */, 128);

   if (serial_port_tasklet_runner < 0)
      panic("Serial: Unable to create a tasklet runner thread for IRQs");

   irq_install_handler(X86_PC_COM1_IRQ, &serial_com1_irq_handler);
   irq_install_handler(X86_PC_COM2_IRQ, &serial_com2_irq_handler);
}
