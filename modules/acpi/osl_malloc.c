/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/list.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/errno.h>

#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/acpiosxf.h>

void *
AcpiOsAllocate(ACPI_SIZE Size)
{
   const size_t sz = (size_t)Size;
   void *vaddr;
   ulong var;

   if (Size >= 512 * MB) {

      /*
       * Don't allow that, ever. Note some kind of check is mandatory, even if
       * the machine has more than 512 MB of contiguous free memory, just
       * because ACPI_SIZE is 64-bit wide, while kmalloc()'s size parameter
       * is a pointer-size integer. In general, ACPICA shouldn't even consider
       * allocating so large chunks of memory, no matter what. ACPI_SIZE is
       * 64-bit wide just because it's used for other purposes as well (memory
       * regions, even on 32-bit systems, can indeed be larger than 2^32 bytes).
       */
      return NULL;
   }

   // if (in_irq()) {
   //    printk("ACPI: AcpiOsAllocate(%zu) called in IRQ context\n", sz);
   // }

   disable_interrupts(&var);
   {
      vaddr = kmalloc(sz);
   }
   enable_interrupts(&var);
   return vaddr;
}

void
AcpiOsFree(void *Memory)
{
   ulong var;
   disable_interrupts(&var);
   {
      kfree(Memory);
   }
   enable_interrupts(&var);
}

ACPI_STATUS
osl_init_malloc(void)
{
   return AE_OK;
}
