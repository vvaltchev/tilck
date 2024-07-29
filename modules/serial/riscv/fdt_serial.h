/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/irq.h>

struct fdt_serial {
   struct list_node node;
   const struct fdt_match *id_table;
   int (*init)(void *fdt, int node, const struct fdt_match *match);
};

struct fdt_serial_ops {
   bool (*rx_rdy)(void *priv);
   char (*rx_c)(void *priv);
   void (*tx_c)(void *priv, char c);
};

struct fdt_serial_dev {
   void *priv;
   struct list_node node;

   int x86_irq;
   u16 x86_port;

   struct irq_handler_node irq_node;
   struct fdt_serial_ops *ops;
};

#define REGISTER_FDT_SERIAL(__name, __id_table, __init)      \
                                                          \
   static struct fdt_serial fdt_serial_##__name = {             \
      .id_table = __id_table,                             \
      .init = __init                                      \
   };                                                     \
                                                          \
   __attribute__((constructor))                           \
   static void __register_fdt_serial_##__name(void) {        \
      fdt_serial_drv_register(&fdt_serial_##__name);            \
   }

void fdt_serial_drv_register(struct fdt_serial *drv);
int fdt_serial_register(void *priv, struct fdt_serial_ops *ops,
                        int irq, irq_handler_t handler);
enum irq_action fdt_serial_generic_irq_handler(void *ctx);

