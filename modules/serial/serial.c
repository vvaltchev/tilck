/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/modules.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/worker_thread.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/sched.h>

#include <tilck/mods/serial.h>

/* NOTE: hw-specific stuff in generic code. TODO: fix that. */

struct serial_device {

   const char *name;
   u16 ioport;
   struct tty *tty;
   ATOMIC(int) jobs_cnt;
   int worker_thread_id;
};

struct serial_device legacy_serial_ports[] =
{
   {
      .name = "COM1",
      .ioport = COM1,
   },
   {
      .name = "COM2",
      .ioport = COM2,
   },
   {
      .name = "COM3",
      .ioport = COM3,
   },
   {
      .name = "COM4",
      .ioport = COM4,
   },
};

static void ser_bh_handler(void *ctx)
{
   struct serial_device *const dev = ctx;
   struct tty *const t = dev->tty;
   const u16 p = dev->ioport;
   char c;

   while (serial_read_ready(p)) {

      c = serial_read(p);
      tty_send_keyevent(t, make_key_event(0, c, true), true);
   }

   dev->jobs_cnt--;
}

static enum irq_action serial_con_irq_handler(void *ctx)
{
   struct serial_device *const dev = ctx;

   if (!serial_read_ready(dev->ioport))
      return IRQ_UNHANDLED; /* Not an IRQ from this "device" [irq sharing] */

   if (dev->jobs_cnt >= 2)
      return IRQ_FULLY_HANDLED;

   if (!wth_enqueue_job(dev->worker_thread_id, &ser_bh_handler, dev)) {
      printk("[serial] WARNING: hit job queue limit\n");
      return IRQ_FULLY_HANDLED;
   }

   dev->jobs_cnt++;
   return IRQ_REQUIRES_BH;
}

void early_init_serial_ports(void)
{
   init_serial_port(COM1);
   init_serial_port(COM2);
   init_serial_port(COM3);
   init_serial_port(COM4);
}

DEFINE_IRQ_HANDLER_NODE(com1, serial_con_irq_handler, &legacy_serial_ports[0]);
DEFINE_IRQ_HANDLER_NODE(com2, serial_con_irq_handler, &legacy_serial_ports[1]);
DEFINE_IRQ_HANDLER_NODE(com3, serial_con_irq_handler, &legacy_serial_ports[2]);
DEFINE_IRQ_HANDLER_NODE(com4, serial_con_irq_handler, &legacy_serial_ports[3]);

static void init_serial_comm(void)
{
   int worker_thread_id;

   disable_preemption();
   {
      worker_thread_id =
         wth_create_thread(1 /* priority */, WTH_KB_QUEUE_SIZE);

      if (worker_thread_id < 0)
         panic("Serial: Unable to create a worker thread for IRQs");
   }
   enable_preemption();

   for (u32 i = 0; i < ARRAY_SIZE(legacy_serial_ports); i++) {

      struct serial_device *dev = &legacy_serial_ports[i];

      dev->tty = get_serial_tty((int)i);
      dev->worker_thread_id = worker_thread_id;
   }

   irq_install_handler(X86_PC_COM1_COM3_IRQ, &com1);
   irq_install_handler(X86_PC_COM1_COM3_IRQ, &com3);
   irq_install_handler(X86_PC_COM2_COM4_IRQ, &com2);
   irq_install_handler(X86_PC_COM2_COM4_IRQ, &com4);
}

static struct module serial_module = {

   .name = "serial",
   .priority = 400,
   .init = &init_serial_comm,
};

REGISTER_MODULE(&serial_module);
