/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>

#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/acpiosxf.h>

#include <limits.h>           // system header

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


/*
 * ---------------------------------------
 * OSL SEMAPHORE
 * ---------------------------------------
 */


ACPI_STATUS
AcpiOsCreateSemaphore(
    UINT32                  MaxUnits,
    UINT32                  InitialUnits,
    ACPI_SEMAPHORE          *OutHandle)
{
   struct ksem *s;

   if (MaxUnits == ACPI_NO_UNIT_LIMIT)
      MaxUnits = INT_MAX;

   if (MaxUnits > INT_MAX || InitialUnits > INT_MAX || !OutHandle)
      return AE_BAD_PARAMETER;

   if (!(s = kalloc_obj(struct ksem)))
      return AE_NO_MEMORY;

   ksem_init(s, (int)InitialUnits, (int)MaxUnits);
   *OutHandle = s;
   return AE_OK;
}

ACPI_STATUS
AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle)
{
   struct ksem *s = Handle;

   if (!Handle)
      return AE_BAD_PARAMETER;

   ksem_destroy(s);
   kfree2(Handle, sizeof(struct ksem));
   return AE_OK;
}

ACPI_STATUS
AcpiOsWaitSemaphore(
    ACPI_SEMAPHORE          Handle,
    UINT32                  Units,
    UINT16                  Timeout)
{
   struct ksem *s = Handle;
   int rc;

   if (Units > INT_MAX || !Handle)
      return AE_BAD_PARAMETER;

   rc = ksem_wait(s, (int)Units, (int)Timeout);

   switch (rc) {

      case 0:
         return AE_OK;

      case -EINVAL:
         return AE_BAD_PARAMETER;

      case -ETIME:
         return AE_TIME;

      default:
         return AE_ERROR;
   }
}

ACPI_STATUS
AcpiOsSignalSemaphore(
    ACPI_SEMAPHORE          Handle,
    UINT32                  Units)
{
   struct ksem *s = Handle;
   int rc;

   if (Units > INT_MAX || !Handle)
      return AE_BAD_PARAMETER;

   rc = ksem_signal(s, (int)Units);

   switch (rc) {

      case 0:
         return AE_OK;

      case -EINVAL:
         return AE_BAD_PARAMETER;

      case -EDQUOT:
         return AE_LIMIT;

      default:
         return AE_ERROR;
   }
}

