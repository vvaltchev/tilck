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

static enum irq_action serial_con_irq_handler(regs *r, u16 portn)
{
   if (!serial_read_ready(com_ports[portn]))
      return IRQ_UNHANDLED; /* Not an IRQ from this "device" [irq sharing] */

   if (!enqueue_tasklet1(serial_port_tasklet_runner,
                         &serial_con_bh_handler, portn))
   {
      panic("KB: hit tasklet queue limit");
   }

   return IRQ_REQUIRES_BH;
}

static enum irq_action serial_com1_irq_handler(regs *r)
{
   return serial_con_irq_handler(r, 0);
}

static enum irq_action serial_com2_irq_handler(regs *r)
{
   return serial_con_irq_handler(r, 1);
}

static enum irq_action serial_com3_irq_handler(regs *r)
{
   return serial_con_irq_handler(r, 2);
}

static enum irq_action serial_com4_irq_handler(regs *r)
{
   return serial_con_irq_handler(r, 3);
}

void early_init_serial_ports(void)
{
   init_serial_port(COM1);
   init_serial_port(COM2);
   init_serial_port(COM3);
   init_serial_port(COM4);
}

static irq_handler_node serial_irq_handler_nodes[4] =
{
   {
      .node = make_list_node(serial_irq_handler_nodes[0].node),
      .handler = serial_com1_irq_handler,
   },

   {
      .node = make_list_node(serial_irq_handler_nodes[1].node),
      .handler = serial_com2_irq_handler,
   },

   {
      .node = make_list_node(serial_irq_handler_nodes[2].node),
      .handler = serial_com3_irq_handler,
   },

   {
      .node = make_list_node(serial_irq_handler_nodes[3].node),
      .handler = serial_com4_irq_handler,
   }
};

void init_serial_comm(void)
{
   disable_preemption();
   {
      serial_port_tasklet_runner =
         create_tasklet_thread(1 /* priority */, KB_TASKLETS_QUEUE_SIZE);

      if (serial_port_tasklet_runner < 0)
         panic("Serial: Unable to create a tasklet runner thread for IRQs");
   }
   enable_preemption();

   irq_install_handler(X86_PC_COM1_COM3_IRQ, &serial_irq_handler_nodes[0]);
   irq_install_handler(X86_PC_COM1_COM3_IRQ, &serial_irq_handler_nodes[2]);
   irq_install_handler(X86_PC_COM2_COM4_IRQ, &serial_irq_handler_nodes[1]);
   irq_install_handler(X86_PC_COM2_COM4_IRQ, &serial_irq_handler_nodes[3]);
}
