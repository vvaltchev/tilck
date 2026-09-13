/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/timer.h>

int debug_qemu_turn_off_machine(void)
{
   if (!in_hypervisor())
      return -ENXIO;

   outb(0xf4, 0x00);

   /*
    * QEMU up to 8.2 exits inside the port write, so the line above never
    * returns. Since 9.0 the write only REQUESTS a shutdown, carried out
    * by the main loop a moment later while the vCPU keeps running: give
    * it that moment, or the caller goes on as if the write had failed,
    * and a machine about to disappear prints a panic on its way out.
    */
   for (int i = 0; i < 10; i++)
      delay_us(100 * 1000);

   return -EIO;
}

void dump_raw_stack(ulong addr)
{
   printk("Raw stack dump:\n");

   for (int i = 0; i < 36; i += 4) {

      printk("%p: ", TO_PTR(addr));

      for (int j = 0; j < 4; j++) {
         printk("%p ", *(void **)addr);
         addr += sizeof(ulong);
      }

      printk("\n");
   }
}

void dump_eflags(u32 f)
{
   printk("eflags: %p [ %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s], IOPL: %u\n",
          TO_PTR(f),
          f & EFLAGS_CF ? "CF " : "",
          f & EFLAGS_PF ? "PF " : "",
          f & EFLAGS_AF ? "AF " : "",
          f & EFLAGS_ZF ? "ZF " : "",
          f & EFLAGS_SF ? "SF " : "",
          f & EFLAGS_TF ? "TF " : "",
          f & EFLAGS_IF ? "IF " : "",
          f & EFLAGS_DF ? "DF " : "",
          f & EFLAGS_OF ? "OF " : "",
          f & EFLAGS_NT ? "NT " : "",
          f & EFLAGS_RF ? "RF " : "",
          f & EFLAGS_VM ? "VM " : "",
          f & EFLAGS_AC ? "AC " : "",
          f & EFLAGS_VIF ? "VIF " : "",
          f & EFLAGS_VIP ? "VIP " : "",
          f & EFLAGS_ID ? "ID " : "",
          f & EFLAGS_IOPL);
}
