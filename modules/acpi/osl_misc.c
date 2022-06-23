/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/datetime.h>

#include "osl.h"
#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/accommon.h>

ACPI_MODULE_NAME("osl_misc")
static ulong boot_acpi_root_ptr;

void acpi_set_root_pointer(ulong ptr)
{
   boot_acpi_root_ptr = ptr;
}

ACPI_STATUS
AcpiOsInitialize(void)
{
   ACPI_STATUS rc;
   ACPI_FUNCTION_TRACE(__FUNC__);

   if ((rc = osl_init_malloc()) != AE_OK)
      return_ACPI_STATUS(rc);

   if ((rc = osl_init_tasks()) != AE_OK)
      return_ACPI_STATUS(rc);

   if ((rc = osl_init_irqs()) != AE_OK)
      return_ACPI_STATUS(rc);

   return_ACPI_STATUS(AE_OK);
}

ACPI_STATUS
AcpiOsTerminate(void)
{
   ACPI_FUNCTION_TRACE(__FUNC__);
   return_ACPI_STATUS(AE_OK);
}

ACPI_PHYSICAL_ADDRESS
AcpiOsGetRootPointer(void)
{
   if (boot_acpi_root_ptr)
      return boot_acpi_root_ptr;

   ACPI_PHYSICAL_ADDRESS ptr = 0;
   AcpiFindRootPointer(&ptr);
   return ptr;
}

ACPI_STATUS
AcpiOsPredefinedOverride(
    const ACPI_PREDEFINED_NAMES *InitVal,
    ACPI_STRING                 *NewVal)
{
   *NewVal = NULL;
   return AE_OK;
}

ACPI_STATUS
AcpiOsTableOverride(
    ACPI_TABLE_HEADER       *ExistingTable,
    ACPI_TABLE_HEADER       **NewTable)
{
   *NewTable = NULL;
   return AE_OK;
}

ACPI_STATUS
AcpiOsPhysicalTableOverride(
    ACPI_TABLE_HEADER       *ExistingTable,
    ACPI_PHYSICAL_ADDRESS   *NewAddress,
    UINT32                  *NewTableLength)
{
   *NewAddress = 0;
   return AE_OK;
}

void
AcpiOsSleep(UINT64 Milliseconds)
{
   kernel_sleep_ms(Milliseconds);
}

void
AcpiOsStall(UINT32 Microseconds)
{
   disable_preemption();
   {
      UINT32 n = Microseconds / 100000;
      UINT32 rem = Microseconds % 100000;

      for (UINT32 i = 0; i < n; i++)
         delay_us(100000);

      delay_us(rem);
   }
   enable_preemption_nosched();
}

UINT64
AcpiOsGetTimer(void)
{
   /* system time in 100-ns units */
   return get_sys_time() / 100;
}

ACPI_THREAD_ID
AcpiOsGetThreadId(void)
{
   return get_curr_tid();
}

ACPI_STATUS
AcpiOsSignal (
    UINT32                  Function,
    void                    *Info)
{
   if (Function == ACPI_SIGNAL_FATAL) {

      ACPI_SIGNAL_FATAL_INFO *i = Info;

      panic("ACPI: fatal signal. Type: 0x%x, Code: 0x%x, Arg: 0x%x\n",
            i->Type, i->Code, i->Argument);

   } else if (Function == ACPI_SIGNAL_BREAKPOINT) {

      printk("ACPI: breakpoint: %s\n", (char *)Info);
      printk("ACPI: ignoring the breakpoint\n");
      return AE_OK;

   } else {

      panic("ACPI: unknown signal: %u\n", Function);
   }
}

ACPI_STATUS
AcpiOsEnterSleep(
    UINT8                   SleepState,
    UINT32                  RegaValue,
    UINT32                  RegbValue)
{
   ACPI_FUNCTION_TRACE(__FUNC__);
   printk("ACPI sleep: %u, 0x%x, 0x%x\n", SleepState, RegaValue, RegbValue);
   return_ACPI_STATUS(AE_OK);
}

ACPI_PRINTF_LIKE (1)
void ACPI_INTERNAL_VAR_XFACE
AcpiOsPrintf(const char *Format, ...)
{
   va_list args;
   va_start(args, Format);
   vprintk(Format, args);
   va_end(args);
}

void
AcpiOsVprintf(
    const char              *Format,
    va_list                 Args)
{
   vprintk(Format, Args);
}

/* ACPI DEBUGGER funcs: keep not implemented for the moment */

void
AcpiOsRedirectOutput(void *Destination)
{
   printk("AcpiOsRedirectOutput ignored\n");
}

ACPI_STATUS
AcpiOsGetLine (
    char                    *Buffer,
    UINT32                  BufferLength,
    UINT32                  *BytesRead)
{
   NOT_IMPLEMENTED();
}

ACPI_STATUS
AcpiOsInitializeDebugger(void)
{
   /* do nothing */
   return AE_OK;
}

void
AcpiOsTerminateDebugger(void)
{
   /* do nothing */
}

ACPI_STATUS
AcpiOsWaitCommandReady(void)
{
   NOT_IMPLEMENTED();
}

ACPI_STATUS
AcpiOsNotifyCommandComplete(void)
{
   NOT_IMPLEMENTED();
}

void
AcpiOsTracePoint(
    ACPI_TRACE_EVENT_TYPE   Type,
    BOOLEAN                 Begin,
    UINT8                   *Aml,
    char                    *Pathname)
{
   /* do nothing */
}
