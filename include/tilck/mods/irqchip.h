/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/list.h>

#define FDT_MAX_INTERRUPTS_PARAMS 16

enum irq_type {
   IRQ_TYPE_NONE           = 0x00000000,
   IRQ_TYPE_EDGE_RISING    = 0x00000001,
   IRQ_TYPE_EDGE_FALLING   = 0x00000002,
   IRQ_TYPE_EDGE_BOTH      = (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING),
   IRQ_TYPE_LEVEL_HIGH     = 0x00000004,
   IRQ_TYPE_LEVEL_LOW      = 0x00000008,
};

struct fdt_irq_param {
   int intc_node;
   int param_nums;
   u32 params[FDT_MAX_INTERRUPTS_PARAMS];
};

struct fdt_irqchip {
   struct list_node node;
   const struct fdt_match *id_table;
   int (*init)(void *fdt, int node, const struct fdt_match *match);
};

struct fdt_irqchip_ops {
   void (*hwirq_set_mask)(int hwirq, void *priv);
   void (*hwirq_clear_mask)(int hwirq, void *priv);
   bool (*hwirq_is_masked)(int hwirq, void *priv);
   void (*hwirq_set_type)(int hwirq, int type, void *priv);
   int (*xlate_irq_param)(struct fdt_irq_param *param,
                          int *hwirq, int *type, void *priv);
};

struct irq_domain {
   void *priv;
   struct list_node node;
   int fdt_node;
   struct fdt_irqchip_ops *ops;
   size_t irq_map_size;
   int irq_map[];
};

struct irq_data {
   bool present;
   u32 hwirq;
   enum irq_type type;
   u32 unhandled_count;
   struct irq_domain *domain;
};

#define REGISTER_FDT_IRQCHIP(__name, __id_table, __init)  \
                                                          \
   static struct fdt_irqchip fdt_irqchip_##__name = {     \
      .id_table = __id_table,                             \
      .init = __init                                      \
   };                                                     \
                                                          \
   __attribute__((constructor))                           \
   static void __register_fdt_irqchip_##__name(void) {    \
      irqchip_drv_register(&fdt_irqchip_##__name);        \
   }

int fdt_parse_one_hwirq(void *fdt,
                        int node,
                        int index,
                        struct fdt_irq_param *irq_param);

int default_xlate_irq_param(struct fdt_irq_param *param,
                            int *hwirq,
                            int *type,
                            void *priv);

struct irq_domain *
irqchip_register_irq_domain(void *priv,
                            int fdt_node,
                            int int_nums,
                            struct fdt_irqchip_ops *ops);

void irqchip_drv_register(struct fdt_irqchip *drv);
int irqchip_get_irq_count(void *fdt, int node);
int irqchip_alloc_irq(void *fdt, int node, int hwirq_index);
void irqchip_free_irq(int irq);
int irqchip_get_free_irq(struct irq_domain *domain, int hwirq);
void irqchip_put_irq(int irq);
enum irq_action generic_irq_handler(u8 irq);
void init_fdt_irqchip(void);

