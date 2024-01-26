/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>
#include <tilck/common/printk.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/mods/irqchip.h>
#include <3rd_party/fdt_helper.h>
#include <libfdt.h>

#define SPRIORITY_BASE      0
#define SPRIORITY_PER_ID    4

#define SENABLE_BASE        0x2080
#define SENABLE_PER_HART    0x80

#define SCONTEXT_BASE       0x201000
#define SCONTEXT_PER_HART   0x1000
#define SCONTEXT_THRESHOLD  0x00
#define SCONTEXT_CLAIM      0x04

/* Only used to record the boot hart */
struct plic_per_hart {
   int hart_id;
   int irq_index;
   void *priority_base;
   void *context_base;
   void *enable_base;
};

struct plic_base {
   void *base;
   ulong paddr;
   ulong size;
   ulong int_nums;
   struct plic_per_hart plic_hart;
   struct irq_domain *domain;
};

int plic_irq;

static int
fdt_parse_plic(void *fdt, int node, struct plic_base *plic)
{
   int len, rc;
   const fdt32_t *val;
   u64 addr, size;

   if (node < 0 || !plic || !fdt)
      return -ENODEV;

   rc = fdt_get_node_addr_size(fdt, node, 0, &addr, &size);
   if (rc < 0 || !addr || !size)
      return -ENODEV;

   plic->paddr = addr;
   plic->size = size;

   val = fdt_getprop(fdt, node, "riscv,ndev", &len);
   if (!val)
      return -ENODEV;

   plic->int_nums = fdt32_to_cpu(*val);
   return 0;
}

static int
plic_find_hart_irq_index(void *fdt, int node, int irq_cnt, int hartid)
{
   int irq_index, len, rc, intc_node, cpu_node, hwirq, type;
   const fdt32_t *val;
   u64 cpuid;
   struct fdt_irq_param irq_param;

   for (irq_index = 0; irq_index < irq_cnt; irq_index++) {
      rc = fdt_parse_one_hwirq(fdt, node, irq_index, &irq_param);
      if (rc)
         return -EINVAL;

      default_xlate_irq_param(&irq_param, &hwirq, &type, NULL);
      intc_node = irq_param.intc_node;

      cpu_node = fdt_parent_offset(fdt, intc_node);
      if (cpu_node < 0)
         return -EINVAL;

      val = fdt_getprop(fdt, cpu_node, "device_type", &len);
      if (!val)
         return -EINVAL;

      if (strncmp ((char *)val, "cpu", strlen ("cpu")))
         return -EINVAL;

      rc = fdt_get_node_addr_size(fdt, cpu_node, 0, &cpuid, NULL);
      if (rc)
         return -EINVAL;

      if ((cpuid == (u64)hartid) && (hwirq == IRQ_S_EXT))
         return irq_index;
   }

   return -EINVAL;
}

static void plic_set_mask(int hwirq, void *priv)
{
   struct plic_per_hart *plic_hart;
   void *enable, *priority;
   u32 enable_mask;

   plic_hart = (struct plic_per_hart *)priv;
   priority = plic_hart->priority_base + hwirq * SPRIORITY_PER_ID;
   enable = plic_hart->enable_base + (hwirq / 32) * sizeof(u32);
   enable_mask = 1 << (hwirq % 32);
   mmio_writel(0, priority);
   mmio_writel(mmio_readl(enable) & ~enable_mask, enable);
}

static void plic_clear_mask(int hwirq, void *priv)
{
   struct plic_per_hart *plic_hart;
   void *enable, *priority;
   u32 enable_mask;

   plic_hart = (struct plic_per_hart *)priv;
   priority = plic_hart->priority_base + hwirq * SPRIORITY_PER_ID;
   enable = plic_hart->enable_base + (hwirq / 32) * sizeof(u32);
   enable_mask = 1 << (hwirq % 32);
   mmio_writel(1, priority);
   mmio_writel(mmio_readl(enable) | enable_mask, enable);
}

static bool plic_is_masked(int hwirq, void *priv)
{
   struct plic_per_hart *plic_hart;
   void *enable;
   u32 enable_mask;

   plic_hart = (struct plic_per_hart *)priv;
   enable = plic_hart->enable_base + (hwirq / 32) * sizeof(u32);
   enable_mask = 1 << (hwirq % 32);
   return !(mmio_readl(enable) & enable_mask);
}

static enum irq_action plic_irq_handler(void *ctx)
{
   enum irq_action hret = IRQ_NOT_HANDLED;
   struct plic_base *plic;
   int hwirq, irq;
   void *claim;

   ASSERT(!are_interrupts_enabled());
   ASSERT(!is_preemption_enabled());

   plic = (struct plic_base *)ctx;
   claim = plic->plic_hart.context_base + SCONTEXT_CLAIM;

   /*
    * Reading and writing claim register automatically enables
    * and disables the interrupt, nothing else to do.
    */
   hwirq = mmio_readl(claim);
   irq = plic->domain->irq_map[hwirq];
   ASSERT(irq);

   enable_interrupts_forced();
   {
      hret = generic_irq_handler(irq);
   }
   disable_interrupts_forced();
   mmio_writel(hwirq, claim);
   return hret;
}

struct fdt_irqchip_ops plic_ops = {
   .hwirq_set_mask = plic_set_mask,
   .hwirq_clear_mask = plic_clear_mask,
   .hwirq_is_masked = plic_is_masked,
};

DEFINE_IRQ_HANDLER_NODE(plic_irq_node, plic_irq_handler, NULL);

int plic_init(void *fdt, int node, const struct fdt_match *match)
{
   int rc, irq, irq_cnt, irq_index;
   struct plic_base *plic;

   plic = kzalloc_obj(struct plic_base);
   if (!plic) {
      printk("plic: ERROR: unable to alloc plic\n");
      return -ENOMEM;
   }

   rc = fdt_parse_plic(fdt, node, plic);
   if (rc)
      goto bad;

   irq_cnt = irqchip_get_irq_count(fdt, node);
   if (!irq_cnt) {
      printk("plic: ERROR: PLIC has no irq!\n");
      rc = -EINVAL;
      goto bad;
   }

   irq_index = plic_find_hart_irq_index(fdt, node, irq_cnt, get_boothartid());
   if (irq_index < 0) {
      printk("plic: ERROR: plic_find_hart_irq_index()!\n");
      rc = -EINVAL;
      goto bad;
   }

   plic->domain = irqchip_register_irq_domain(&plic->plic_hart,
                                              node,
                                              plic->int_nums,
                                              &plic_ops);
   if (!plic->domain) {
      printk("plic: ERROR: unable to register IRQ domain\n");
      rc = -EINVAL;
      goto bad;
   }

   plic->base = ioremap(plic->paddr, plic->size);
   if (!plic->base) {
      printk("plic: ERROR: ioremap failed for %p\n", (void *)plic->paddr);
      rc = -EIO;
      goto bad0;
   }

   plic->plic_hart.hart_id = get_boothartid();
   plic->plic_hart.irq_index = irq_index;
   plic->plic_hart.priority_base = plic->base + SPRIORITY_BASE;
   plic->plic_hart.context_base = plic->base + SCONTEXT_BASE          \
                                 + get_boothartid() * SCONTEXT_PER_HART * 2;
   plic->plic_hart.enable_base = plic->base + SENABLE_BASE            \
                                 + get_boothartid() * SENABLE_PER_HART * 2;

   /* Set hart threshold to zero */
   mmio_writel(0, plic->plic_hart.context_base + SCONTEXT_THRESHOLD);

   /* Mask all interrupts */
   for (u32 i = 1; i <= plic->int_nums; i++) {
      plic_set_mask(i, &plic->plic_hart);
   }

   /* Alloc a globle irq number */
   irq = irqchip_alloc_irq(fdt, node, irq_index);
   if (irq < 0) {
      printk("plic: ERROR: cannot alloc globle irq number!\n");
      rc = -EINVAL;
      goto bad;
   }

   plic_irq = irq;

   /* setup irq handler */
   plic_irq_node.context = plic;
   irq_install_handler(irq, &plic_irq_node);
   return 0;

bad0:
   kfree(plic->domain);
bad:
   kfree(plic);
   return rc;
}

static const struct fdt_match plic_ids[] = {
   {.compatible = "riscv,plic0"},
   {.compatible = "sifive,plic-1.0.0"},
   {.compatible = "thead,c900-plic"},
   { }
};

REGISTER_FDT_IRQCHIP(plic, plic_ids, plic_init)

