/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/boot.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/modules.h>
#include <tilck/kernel/errno.h>
#include <tilck/mods/serial.h>
#include <tilck/mods/irqchip.h>
#include <3rd_party/fdt_helper.h>
#include <libfdt.h>
#include "fdt_serial.h"

#define X86_PC_COM1 0x3f8
#define X86_PC_COM2 0x2f8
#define X86_PC_COM3 0x3e8
#define X86_PC_COM4 0x2e8

#define X86_PC_COM2_COM4_IRQ       3
#define X86_PC_COM1_COM3_IRQ       4

static struct list drv_list = STATIC_LIST_INIT(drv_list);
static struct list serials = STATIC_LIST_INIT(serials);
static struct fdt_serial_dev *x86port2uart[4];
static bool init_done = false;
int console_irq;

static struct fdt_serial_dev *get_fdt_serial_by_port(u16 port)
{
   struct fdt_serial_dev *uart_find = NULL;

   switch (port) {

      case X86_PC_COM1:
         uart_find = x86port2uart[0];
         break;

      case X86_PC_COM2:
         uart_find = x86port2uart[1];
         break;

      case X86_PC_COM3:
         uart_find = x86port2uart[2];
         break;

      case X86_PC_COM4:
         uart_find = x86port2uart[3];
         break;

      default:
         return x86port2uart[0];
   }

   return uart_find;
}

void init_serial_port(u16 port)
{
   /* do nothing */
}

bool serial_read_ready(u16 port)
{
   struct fdt_serial_dev *uart;

   uart = get_fdt_serial_by_port(port);
   return uart->ops->rx_rdy(uart->priv);
}

void serial_wait_for_read(u16 port)
{
   /* do nothing */
}

bool serial_write_ready(u16 port)
{
   /* do nothing */
   return 0;
}

void serial_wait_for_write(u16 port)
{
   /* do nothing */
}

char serial_read(u16 port)
{
   struct fdt_serial_dev *uart;

   uart = get_fdt_serial_by_port(port);
   return uart->ops->rx_c(uart->priv);
}

void serial_write(u16 port, char c)
{
   struct fdt_serial_dev *uart;

   if (!init_done) {
      sbi_console_putchar(c);
      return;
   }

   uart = get_fdt_serial_by_port(port);
   uart->ops->tx_c(uart->priv, c);
}

enum irq_action fdt_serial_generic_irq_handler(void *ctx)
{
   struct fdt_serial_dev *serial = ctx;
   enum irq_action hret = IRQ_NOT_HANDLED;

   hret = generic_irq_handler(serial->x86_irq);
   return hret;
}

int fdt_serial_register(void *priv,
                        struct fdt_serial_ops *ops,
                        int irq, irq_handler_t handler)
{
   struct fdt_serial_dev *serial;
   static int cnt = 0;

   serial = kzmalloc(sizeof(*serial));
   if (!serial)
      return -ENOMEM;

   ++cnt;
   if (cnt == 1) {
      serial->x86_port = X86_PC_COM1;
      serial->x86_irq = X86_PC_COM1_COM3_IRQ;
      x86port2uart[0] = serial;
      console_irq = irq;
   } else if (cnt == 2) {
      serial->x86_port = X86_PC_COM2;
      serial->x86_irq = X86_PC_COM2_COM4_IRQ;
      x86port2uart[1] = serial;
   } else if (cnt == 3) {
      serial->x86_port = X86_PC_COM3;
      serial->x86_irq = X86_PC_COM1_COM3_IRQ;
      x86port2uart[2] = serial;
   } else if (cnt == 4) {
      serial->x86_port = X86_PC_COM4;
      serial->x86_irq = X86_PC_COM2_COM4_IRQ;
      x86port2uart[3] = serial;
   } else {
      return -ENODEV;
   }

   serial->priv = priv;
   serial->ops = ops;
   serial->irq_node.handler = handler;
   serial->irq_node.context = serial;
   list_node_init(&serial->irq_node.node);

   irq_install_handler(irq, &serial->irq_node);

   disable_preemption();
   list_add_tail(&serials, &serial->node);
   enable_preemption();

   init_done = true;
   return 0;
}

void fdt_serial_drv_register(struct fdt_serial *drv)
{
   list_add_tail(&drv_list, &drv->node);
}

static void init_fdt_serial(void)
{
   int node;
   bool enabled;
   const struct fdt_match *matched;
   struct fdt_serial *drv;

   void *fdt = fdt_get_address();

   for (node = fdt_next_node(fdt, -1, NULL);
        node >= 0;
        node = fdt_next_node(fdt, node, NULL)) {

      list_for_each_ro(drv, &drv_list, node) {
         enabled = fdt_node_is_enabled(fdt, node);
         matched = fdt_match_node(fdt, node, drv->id_table);

         if (enabled && matched) {
            drv->init(fdt, node, matched);
            break;
         }
      }
   }
}

static struct module fdt_serial_module = {

   .name = "fdt_serial",
   .priority = MOD_serial_prio + 1,
   .init = &init_fdt_serial,
};

REGISTER_MODULE(&fdt_serial_module);

