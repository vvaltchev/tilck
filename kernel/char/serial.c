/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/serial.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/cmdline.h>

extern int kb_tasklet_runner;


static void serial_con_bh_handler(char c)
{
   tty_keypress_handler_int(get_curr_tty(), (u32)c, (u8)c, false);
}

static int serial_con_irq_handler(regs *r, u16 port)
{
   char c;

   if (!serial_read_ready(port))
      return 0;

   while (serial_read_ready(port)) {

      c = serial_read(port);

      if (!enqueue_tasklet1(kb_tasklet_runner, &serial_con_bh_handler, c))
         panic("KB: hit tasklet queue limit");
   }

   return 1;
}

static int generic_serial_irq_handler(regs *r, u16 port)
{
   int c = 0;

   while (serial_read_ready(port)) {
      serial_read(port);
      c++;
   }

   printk("[Serial @0x%x]: irq. Read count: %d\n", port, c);
   return 0;
}

typedef int (*ser_irq_handler)(regs *, u16);

static ser_irq_handler ser_handlers[4] = {
   generic_serial_irq_handler,
   generic_serial_irq_handler,
   NULL,
   NULL
};

static int serial_irq_dispatcher(regs *r, u32 com_n1)
{
   static const u16 pc_com_ports[4] = {COM1, COM2, COM3, COM4};

   if (!ser_handlers[com_n1])
      return 0;

   return ser_handlers[com_n1](r, pc_com_ports[com_n1]);
}

static int serial_com1_irq_handler(regs *r)
{
   return serial_irq_dispatcher(r, 0);
}

static int serial_com2_irq_handler(regs *r)
{
   return serial_irq_dispatcher(r, 1);
}

void early_init_serial_ports(void)
{
   init_serial_port(COM1);
   init_serial_port(COM2);
}

void init_serial_comm(void)
{
   if (kopt_serial_mode == TERM_SERIAL_CONSOLE)
      ser_handlers[0] = serial_con_irq_handler;

   irq_install_handler(X86_PC_COM1_IRQ, &serial_com1_irq_handler);
   irq_install_handler(X86_PC_COM2_IRQ, &serial_com2_irq_handler);
}
