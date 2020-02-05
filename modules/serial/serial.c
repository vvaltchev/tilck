/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/modules.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/tty.h>

#include <tilck/mods/serial.h>

/* NOTE: hw-specific stuff in generic code. TODO: fix that. */
static const u16 com_ports[] = {COM1, COM2, COM3, COM4};

static int serial_port_tasklet_runner;
static struct tty *serial_ttys[4];
static ATOMIC(int) tasklets_per_port[4];

static void serial_con_bh_handler(u16 portn)
{
   struct tty *const t = serial_ttys[portn];
   const u16 p = com_ports[portn];
   char c;

   while (serial_read_ready(p)) {

      c = serial_read(p);
      tty_send_keyevent(t, make_key_event(0, c, true), true);
   }

   tasklets_per_port[portn]--;
}

static enum irq_action serial_con_irq_handler(regs_t *r, u16 portn)
{
   if (!serial_read_ready(com_ports[portn]))
      return IRQ_UNHANDLED; /* Not an IRQ from this "device" [irq sharing] */

   if (tasklets_per_port[portn] >= 2)
      return IRQ_FULLY_HANDLED;

   if (!enqueue_tasklet1(serial_port_tasklet_runner,
                         &serial_con_bh_handler, portn))
   {
      panic("[serial] hit tasklet queue limit");
   }

   tasklets_per_port[portn]++;
   return IRQ_REQUIRES_BH;
}

static enum irq_action serial_com1_irq_handler(regs_t *r)
{
   return serial_con_irq_handler(r, 0);
}

static enum irq_action serial_com2_irq_handler(regs_t *r)
{
   return serial_con_irq_handler(r, 1);
}

static enum irq_action serial_com3_irq_handler(regs_t *r)
{
   return serial_con_irq_handler(r, 2);
}

static enum irq_action serial_com4_irq_handler(regs_t *r)
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

static struct irq_handler_node serial_irq_handler_nodes[4] =
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

static void init_serial_comm(void)
{
   disable_preemption();
   {
      serial_port_tasklet_runner =
         create_tasklet_thread(1 /* priority */, KB_TASKLETS_QUEUE_SIZE);

      if (serial_port_tasklet_runner < 0)
         panic("Serial: Unable to create a tasklet runner thread for IRQs");
   }
   enable_preemption();

   for (int i = 0; i < 4; i++)
      serial_ttys[i] = get_serial_tty(i);

   irq_install_handler(X86_PC_COM1_COM3_IRQ, &serial_irq_handler_nodes[0]);
   irq_install_handler(X86_PC_COM1_COM3_IRQ, &serial_irq_handler_nodes[2]);
   irq_install_handler(X86_PC_COM2_COM4_IRQ, &serial_irq_handler_nodes[1]);
   irq_install_handler(X86_PC_COM2_COM4_IRQ, &serial_irq_handler_nodes[3]);
}

static struct module serial_module = {

   .name = "serial",
   .priority = 400,
   .init = &init_serial_comm,
};

REGISTER_MODULE(&serial_module);
