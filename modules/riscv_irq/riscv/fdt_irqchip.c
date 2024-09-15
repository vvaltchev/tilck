/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/boot.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>
#include <tilck/kernel/modules.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/sched.h>
#include <tilck/mods/irqchip.h>
#include <3rd_party/fdt_helper.h>
#include <libfdt.h>

#define MAX_IRQCHIP_NUM 16

struct intc_dev {
   int intc;
   int parent_intc;
   struct fdt_irqchip *drv;
   const struct fdt_match *match;
};

struct irq_data irq_datas[MAX_IRQ_NUM];
ulong irq_bitmap[MAX_IRQ_NUM / (sizeof(ulong) * 8)];

u32 spur_irq_count = 0;
u32 unhandled_irq_count[MAX_IRQ_NUM];

static struct list drv_list = STATIC_LIST_INIT(drv_list);
static struct list irq_domains = STATIC_LIST_INIT(irq_domains);

int irqchip_get_free_irq(struct irq_domain *domain, int hwirq)
{
   for (int i = 0; i < ARRAY_SIZE(irq_bitmap); i++) {

      if (~irq_bitmap[i]) {
         ulong idx = get_first_zero_bit_index_l(irq_bitmap[i]);
         int irq = (int)(i * NBITS + idx);

         irq_bitmap[i] |= 1 << idx;
         irq_datas[irq].hwirq = hwirq;
         irq_datas[irq].domain = domain;
         irq_datas[irq].present = true;
         return irq;
      }
   }

   return -EINVAL;
}

void irqchip_put_irq(int irq)
{
   ASSERT(irq > 0 && irq < MAX_IRQ_NUM);
   ulong slot = irq / NBITS;
   ulong index = irq % NBITS;

   irq_bitmap[slot] &= ~(1 << index);
   irq_datas[irq].present = false;
}

enum irq_action generic_irq_handler(u8 irq)
{
   enum irq_action hret = IRQ_NOT_HANDLED;
   struct irq_handler_node *pos;

   list_for_each_ro(pos, &irq_handlers_lists[irq], node) {

      hret = pos->handler(pos->context);
      if (hret != IRQ_NOT_HANDLED)
         break;
   }

   if (hret == IRQ_NOT_HANDLED) {
      irq_datas[irq].unhandled_count++;
      unhandled_irq_count[irq]++;
   }

   return hret;
}

int default_xlate_irq_param(struct fdt_irq_param *param,
                            int *hwirq,
                            int *type,
                            void *priv)
{
   if (param->param_nums == 1) {

      *hwirq = param->params[0];
      *type = IRQ_TYPE_NONE;

   } else if (param->param_nums == 2) {

      *hwirq = param->params[0];
      *type = param->params[1];

   } else {

      printk("irqchip: ERROR: wrong irq parammetet numbers\n");
      return -EINVAL;
   }

   return 0;
}

/*
 * Only "interrupts-extended" and "interrupts" formats have been parsed here,
 * and the "interrupt-map" format has not been processed.
 */
int fdt_parse_one_hwirq(void *fdt, int node, int index,
                        struct fdt_irq_param *irq_param)
{
   int len, rc;
   int intc_node, parent_node;
   const fdt32_t *val = NULL;
   u32 ints_cells, intc_phandle;
   struct fdt_phandle_args pargs;

   /* 1. Try parse "interrupts-extended" fdt format */

   rc = fdt_parse_phandle_with_args(fdt, node, "interrupts-extended",
                                    "#interrupt-cells", index, &pargs);

   if (!rc) {
      irq_param->intc_node = pargs.node_offset;
      irq_param->param_nums = pargs.args_count;

      for (int i = 0; i < pargs.args_count; i++) {
         irq_param->params[i] = pargs.args[i];
      }
      return 0;
   }

   /* 2. Try parse "interrupts" fdt format */

   for (parent_node = node;
        fdt_node_depth(fdt, parent_node) > 0;
        parent_node = fdt_parent_offset(fdt, parent_node))
   {
      val = fdt_getprop(fdt, parent_node, "interrupt-parent", &len);
      if (val) {
         intc_phandle = fdt32_to_cpu(*val);
         break;
      }
   }

   if (!val)
      return -ENODEV;

   intc_node = fdt_node_offset_by_phandle(fdt, intc_phandle);
   if (intc_node < 0 || fdt_node_depth(fdt, parent_node) <= 0)
      return -ENODEV;

   val = fdt_getprop(fdt, intc_node, "#interrupt-cells", &len);
   if (!val)
      return -ENODEV;

   ints_cells = fdt32_to_cpu(*val);
   if (ints_cells > FDT_MAX_INTERRUPTS_PARAMS)
      return -EINVAL;

   val = fdt_getprop(fdt, node, "interrupts", &len);
   if (!val)
      return -ENODEV;

   // Check if the index is within the parameter range
   if (index * ints_cells >= len / sizeof(*val))
      return -EINVAL;

   for (u32 i = 0; i < ints_cells; i++)
      irq_param->params[i] = fdt32_to_cpu(val[index * ints_cells + i]);

   irq_param->intc_node = intc_node;
   irq_param->param_nums = ints_cells;
   return 0;
}

int irqchip_get_irq_count(void *fdt, int node)
{
   struct fdt_irq_param irq_param;
   int index = 0;
   int rc = 0;

   while (rc == 0) {
      rc = fdt_parse_one_hwirq(fdt, node, index, &irq_param);
      index++;
   }

   return index;
}

int irqchip_alloc_irq(void *fdt, int node, int hwirq_index)
{
   struct irq_domain *domain, *tmp;
   struct fdt_irq_param irq_param;
   int irq = 0, rc;
   int hwirq, type;

   rc = fdt_parse_one_hwirq(fdt, node, hwirq_index, &irq_param);
   if (rc)
      return 0;

   disable_preemption();

   /* Find the corresponding irq domain */
   list_for_each(domain, tmp, &irq_domains, node) {

      if (domain->fdt_node != irq_param.intc_node)
         continue;

      if (domain->ops->xlate_irq_param) {
         rc = domain->ops->xlate_irq_param(&irq_param, &hwirq,
                                           &type, domain->priv);
      } else {
         rc = default_xlate_irq_param(&irq_param, &hwirq,
                                      &type, NULL);
      }

      if (rc) {
         enable_preemption();
         return 0;
      }

      irq = irqchip_get_free_irq(domain, hwirq);
      if (irq < 0) {
         enable_preemption();
         return -EINVAL;
      }

      if (domain->ops->hwirq_set_type)
         domain->ops->hwirq_set_type(hwirq, type, domain->priv);

      domain->irq_map[hwirq] = irq;
      break;
   }

   enable_preemption();
   return irq;
}

void irqchip_free_irq(int irq)
{
   struct irq_domain *domain, *tmp;

   disable_preemption();

   list_for_each(domain, tmp, &irq_domains, node) {

      for (u32 i = 0; i < domain->irq_map_size; i++) {

         if (domain->irq_map[i] == irq) {

            domain->irq_map[i] = 0;
            irqchip_put_irq(irq);
            enable_preemption();
            return;
         }
      }
   }

   enable_preemption();
}

struct irq_domain *
irqchip_register_irq_domain(void *priv,
                            int fdt_node,
                            int int_nums,
                            struct fdt_irqchip_ops *ops)
{
   struct irq_domain *domain;

   domain = kzmalloc(sizeof(*domain) + int_nums * sizeof(int));
   if (!domain)
      return NULL;

   domain->fdt_node = fdt_node;
   domain->irq_map_size = int_nums;
   domain->ops = ops;
   domain->priv = priv;

   disable_preemption();
   list_add_tail(&irq_domains, &domain->node);
   enable_preemption();
   return domain;
}

void irqchip_drv_register(struct fdt_irqchip *drv)
{
   list_add_tail(&drv_list, &drv->node);
}

void init_fdt_irqchip(void)
{
   const struct fdt_match *matched;
   struct fdt_irqchip *drv;
   struct fdt_irq_param irq_param;
   const fdt32_t *val;
   struct intc_dev *intc_nodes, *intc_done;
   int node, parent_intc, len;
   bool enabled;
   int intc_cnt = 0;
   int intc_done_index = 0;
   int intc_done_cnt = 0;

   void *fdt = fdt_get_address();

   intc_nodes = kzmalloc(MAX_IRQCHIP_NUM * sizeof(*intc_nodes));
   if (!intc_nodes) {
      printk("irqchip: ERROR: failed to alloc intc_nodes!\n");
      return;
   }

   intc_done = kzmalloc(MAX_IRQCHIP_NUM * sizeof(*intc_done));
   if (!intc_done) {
      printk("irqchip: ERROR: failed to alloc intc_done!\n");
      return;
   }

   for (node = fdt_next_node(fdt, -1, NULL);
        node >= 0;
        node = fdt_next_node(fdt, node, NULL))
   {
      list_for_each_ro(drv, &drv_list, node) {

         enabled = fdt_node_is_enabled(fdt, node);
         matched = fdt_match_node(fdt, node, drv->id_table);
         val = fdt_getprop(fdt, node, "interrupt-controller", &len);

         if (enabled && matched && val) {
            if (fdt_parse_one_hwirq(fdt, node, 0, &irq_param) == 0) {
               intc_nodes[intc_cnt].parent_intc = irq_param.intc_node;
            } else {
               //root interrupt controller
               intc_nodes[intc_cnt].parent_intc = -1;
            }

            intc_nodes[intc_cnt].intc = node;
            intc_nodes[intc_cnt].drv = drv;
            intc_nodes[intc_cnt].match = matched;
            intc_cnt++;
            break;
         }
      }
   }

   intc_done[0].intc = -1;

   /*
    * Initialization is performed sequentially from the root node
    * according to the dependencies of the interrupt controller.
    */
   while (intc_done_cnt < intc_cnt) {

      parent_intc = intc_done[intc_done_index].intc;
      ASSERT(intc_done_index <= intc_done_cnt);
      intc_done_index++;

      for (int i = 0; i < intc_cnt; i++) {
         if (intc_nodes[i].parent_intc == parent_intc) {

            intc_done[intc_done_cnt + 1] = intc_nodes[i];
            intc_done_cnt++;
            intc_nodes[i].drv->init(fdt, intc_nodes[i].intc,
                                    intc_nodes[intc_cnt].match);
         }
      }
   }

   kfree(intc_nodes);
   kfree(intc_done);
}

