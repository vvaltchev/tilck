/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/timer.h>

#include <limits.h>           // system header

#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/accommon.h>

ACPI_MODULE_NAME("osl_sync")

/*
 * ---------------------------------------
 * OSL SPINLOCK
 * ---------------------------------------
 */

ACPI_STATUS
AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle)
{
   ACPI_FUNCTION_TRACE(__FUNC__);

   if (!OutHandle)
      return_ACPI_STATUS(AE_BAD_PARAMETER);

   /*
    * Tilck does not support SMP, therefore there's no need for real spinlocks:
    * disabling the interrupts is enough. Hopefully, ACPI will accept a NULL
    * value, by treating the handle as completely opaque value.
    */
   *OutHandle = NULL;
   return_ACPI_STATUS(AE_OK);
}

void
AcpiOsDeleteLock(ACPI_SPINLOCK Handle)
{
   ACPI_FUNCTION_TRACE(__FUNC__);
}

ACPI_CPU_FLAGS
AcpiOsAcquireLock(ACPI_SPINLOCK Handle)
{
   ulong flags;
   ACPI_FUNCTION_TRACE(__FUNC__);
   disable_interrupts(&flags);
   return_VALUE(flags);
}

void
AcpiOsReleaseLock(
    ACPI_SPINLOCK           Handle,
    ACPI_CPU_FLAGS          Flags)
{
   ulong flags = (ulong) Flags;
   enable_interrupts(&flags);
   ACPI_FUNCTION_TRACE(__FUNC__);
   return_VOID;
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
   ACPI_FUNCTION_TRACE(__FUNC__);

   if (MaxUnits == ACPI_NO_UNIT_LIMIT)
      MaxUnits = INT_MAX;

   if (MaxUnits > INT_MAX || InitialUnits > INT_MAX || !OutHandle)
      return_ACPI_STATUS(AE_BAD_PARAMETER);

   if (!(s = kalloc_obj(struct ksem)))
      return_ACPI_STATUS(AE_NO_MEMORY);

   ksem_init(s, (int)InitialUnits, (int)MaxUnits);
   *OutHandle = s;
   return_ACPI_STATUS(AE_OK);
}

ACPI_STATUS
AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle)
{
   struct ksem *s = Handle;
   ACPI_FUNCTION_TRACE(__FUNC__);

   if (!Handle)
      return_ACPI_STATUS(AE_BAD_PARAMETER);

   ksem_destroy(s);
   kfree2(Handle, sizeof(struct ksem));
   return_ACPI_STATUS(AE_OK);
}

ACPI_STATUS
AcpiOsWaitSemaphore(
    ACPI_SEMAPHORE          Handle,
    UINT32                  Units,
    UINT16                  Timeout)
{
   struct ksem *s = Handle;
   u64 ticks;
   int rc;

   ACPI_FUNCTION_TRACE(__FUNC__);

   if (Units > INT_MAX || !Handle)
      return_ACPI_STATUS(AE_BAD_PARAMETER);

   ticks = ms_to_ticks(Timeout);

   if (ticks > INT_MAX)
      return_ACPI_STATUS(AE_BAD_PARAMETER);

   rc = ksem_wait(s, (int)Units, (int)ticks);

   switch (rc) {

      case 0:
         return_ACPI_STATUS(AE_OK);

      case -EINVAL:
         return_ACPI_STATUS(AE_BAD_PARAMETER);

      case -ETIME:
         return_ACPI_STATUS(AE_TIME);

      default:
         return_ACPI_STATUS(AE_ERROR);
   }
}

ACPI_STATUS
AcpiOsSignalSemaphore(
    ACPI_SEMAPHORE          Handle,
    UINT32                  Units)
{
   struct ksem *s = Handle;
   int rc;

   ACPI_FUNCTION_TRACE(__FUNC__);

   if (Units > INT_MAX || !Handle)
      return_ACPI_STATUS(AE_BAD_PARAMETER);

   rc = ksem_signal(s, (int)Units);

   switch (rc) {

      case 0:
         return_ACPI_STATUS(AE_OK);

      case -EINVAL:
         return_ACPI_STATUS(AE_BAD_PARAMETER);

      case -EDQUOT:
         return_ACPI_STATUS(AE_LIMIT);

      default:
         return_ACPI_STATUS(AE_ERROR);
   }
}

