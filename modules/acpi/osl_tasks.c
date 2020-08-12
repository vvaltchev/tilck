/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/worker_thread.h>
#include <tilck/mods/acpi.h>

#include <3rd_party/acpi/acpi.h>
#include <3rd_party/acpi/accommon.h>

ACPI_MODULE_NAME("osl_tasks")

static struct worker_thread *wth_events;
static struct worker_thread *wth_main;
static struct worker_thread *wth_debug;

ACPI_STATUS
AcpiOsExecute(
    ACPI_EXECUTE_TYPE       Type,
    ACPI_OSD_EXEC_CALLBACK  Function,
    void                    *Context)
{
   struct worker_thread *wth;
   ACPI_FUNCTION_TRACE(__FUNC__);

   if (!Function)
      return_ACPI_STATUS(AE_BAD_PARAMETER);

   switch (Type) {

      case OSL_NOTIFY_HANDLER:
      case OSL_GPE_HANDLER:
         wth = wth_events;
         break;

      case OSL_DEBUGGER_MAIN_THREAD:
      case OSL_DEBUGGER_EXEC_THREAD:
         wth = wth_debug;
         break;

      case OSL_GLOBAL_LOCK_HANDLER: /* fall-through */
      case OSL_EC_POLL_HANDLER:     /* fall-through */
      case OSL_EC_BURST_HANDLER:    /* fall-through */
      default:
         wth = wth_main;
         break;
   }

   if (!wth_enqueue_on(wth, Function, Context))
      panic("AcpiOsExecute: unable to enqueue job");

   return_ACPI_STATUS(AE_OK);
}

void
AcpiOsWaitEventsComplete(void)
{
   ACPI_FUNCTION_TRACE(__FUNC__);

   wth_wait_for_completion(wth_events);
   wth_wait_for_completion(wth_main);
   wth_wait_for_completion(wth_debug);
}

static struct worker_thread *
osl_create_worker_or_die(const char *name, int prio, u16 queue_size)
{
   struct worker_thread *wth;

   if (!(wth = wth_create_thread(name, prio, queue_size)))
      panic("ACPI: unable to create worker thread '%s'", name);

   return wth;
}

ACPI_STATUS
osl_init_tasks(void)
{
   wth_events = osl_create_worker_or_die("acevents", 1, 64);
   wth_main = osl_create_worker_or_die("acmain", 2, 64);

   if (ACPI_DEBUGGER_ENABLED)
      wth_debug = osl_create_worker_or_die("acdebug", 3, 64);

   return AE_OK;
}
