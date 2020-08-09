/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/sync.h>

#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/acpiosxf.h>

/*
 * ---------------------------------------
 * OSL SPINLOCK
 * ---------------------------------------
 */

ACPI_STATUS
AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle)
{
   if (!OutHandle)
      return AE_BAD_PARAMETER;

   /*
    * Tilck does not support SMP, therefore there's no need for real spinlocks:
    * disabling the interrupts is enough. Hopefully, ACPI will accept a NULL
    * value, by treating the handle as completely opaque value.
    */
   *OutHandle = NULL;
   return AE_OK;
}

void
AcpiOsDeleteLock(ACPI_SPINLOCK Handle)
{
   /* Nothing to do */
}

ACPI_CPU_FLAGS
AcpiOsAcquireLock(ACPI_SPINLOCK Handle)
{
   ulong flags;
   disable_interrupts(&flags);
   return flags;
}

void
AcpiOsReleaseLock(
    ACPI_SPINLOCK           Handle,
    ACPI_CPU_FLAGS          Flags)
{
   ulong flags = (ulong) Flags;
   enable_interrupts(&flags);
}
