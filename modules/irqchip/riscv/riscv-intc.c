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
#include <3rd_party/fdt_helper.h>
#include <libfdt.h>

#include <tilck/mods/irqchip.h>

#define INTERRUPT_CAUSE_FLAG	(1UL << (__riscv_xlen - 1))

struct irq_domain *root_domain;

static void hart_set_mask(int hwirq, void *priv)
{
   csr_clear(CSR_SIE, 1 << hwirq);
}

static void hart_clear_mask(int hwirq, void *priv)
{
   csr_set(CSR_SIE, 1 << hwirq);
}

static bool hart_is_masked(int hwirq, void *priv)
{
   return !(csr_read(CSR_SIE) & (1 << hwirq));
}

void arch_irq_handling(regs_t *r)
{
   int hwirq, irq;

   ASSERT(!are_interrupts_enabled());
   ASSERT(!is_preemption_enabled());

   hwirq = r->scause & ~INTERRUPT_CAUSE_FLAG;
   irq = root_domain->irq_map[hwirq];
   ASSERT(irq);

   push_nested_interrupt(regs_intnum(r));
   {
      generic_irq_handler(irq);
   }
   pop_nested_interrupt();
}

struct fdt_irqchip_ops riscv_intc_ops = {
   .hwirq_set_mask = hart_set_mask,
   .hwirq_clear_mask = hart_clear_mask,
   .hwirq_is_masked = hart_is_masked,
};

int riscv_intc_init(void *fdt, int node, const struct fdt_match *match)
{
   int len, rc, cpu_node;
   const fdt32_t *val;
   u64 cpuid;

   /* Find which hart is doing initialize */

   cpu_node = fdt_parent_offset(fdt, node);
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

   /* We only initialize riscv intc on the boot HART */
   if (cpuid != (u64)get_boothartid())
      return 0;

   root_domain = irqchip_register_irq_domain(NULL,
                                             node,
                                             16,
                                             &riscv_intc_ops);
   if (!root_domain) {
      printk("riscv-intc: ERROR: unable to register IRQ domain\n");
      return -EINVAL;
   }

   /* Mask all interrupts */
   for (int i = 0; i < 16; i++) {
      hart_set_mask(i, NULL);
   }

   return 0;
}

static const struct fdt_match riscv_intc_ids[] = {
   {.compatible = "riscv,cpu-intc"},
   { }
};

REGISTER_FDT_IRQCHIP(riscv_intc, riscv_intc_ids, riscv_intc_init)

