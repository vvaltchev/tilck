/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>
#include <tilck/kernel/modules.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/datetime.h>
#include <3rd_party/fdt_helper.h>
#include <libfdt.h>

#include <tilck/mods/irqchip.h>

#define X86_PC_TIMER_IRQ           0

extern struct irq_domain *root_domain;

static ulong riscv_timebase;
static ulong riscv_hz;

static enum irq_action riscv_timer_irq_handler(void *ctx)
{
   enum irq_action hret = IRQ_NOT_HANDLED;

   csr_clear(CSR_SIE, IE_TIE);
   enable_interrupts_forced();

   hret = generic_irq_handler(X86_PC_TIMER_IRQ);

   disable_interrupts_forced();
   csr_set(CSR_SIE, IE_TIE);

   sbi_set_timer(rdtime() + riscv_timebase / riscv_hz);

   return hret;
}

static ulong fdt_parse_timebase_frequency(void)
{
   int cpus_node, len;
   const fdt32_t *prop;
   void *fdt = fdt_get_address();

   cpus_node = fdt_path_offset(fdt, "/cpus");
   if (cpus_node < 0)
      return 0;

   prop = fdt_getprop(fdt, cpus_node, "timebase-frequency", &len);
   if (prop && len)
      return fdt32_to_cpu(*prop);

   return 0;
}

DEFINE_IRQ_HANDLER_NODE(riscv_timer_irq_node, riscv_timer_irq_handler, NULL);

u32 hw_timer_setup(u32 interval)
{
   int irq;
   u64 actual_interval;

   riscv_timebase = fdt_parse_timebase_frequency();
   riscv_hz = TS_SCALE / interval;
   actual_interval = TS_SCALE;
   actual_interval *= riscv_timebase / riscv_hz;
   actual_interval /= riscv_timebase;

   ASSERT(IN_RANGE_INC(riscv_hz, 18, 1000));
   ASSERT(actual_interval < UINT32_MAX);

   irq = irqchip_get_free_irq(root_domain, IRQ_S_TIMER);
   root_domain->irq_map[IRQ_S_TIMER] = irq;

   irq_install_handler(irq, &riscv_timer_irq_node);
   sbi_set_timer(rdtime() + riscv_timebase / riscv_hz);

   return (u32)actual_interval;
}

