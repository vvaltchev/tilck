/* SPDX-License-Identifier: BSD-2-Clause */

#include "defs.h"
#include "utils.h"

#include <tilck/common/printk.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>

NORETURN void panic(const char *fmt, ...)
{
   UINTN mapkey;
   va_list args;

   if (!gExitBootServicesCalled) {

      printk("\n");
      printk("******************* UEFI BOOTLOADER PANIC ********************");
      printk("\n");

      va_start(args, fmt);
      vprintk(fmt, args);
      va_end(args);

      printk("\n");

      if (GetMemoryMap(&mapkey) == EFI_SUCCESS) {

         if (BS->ExitBootServices(gImageHandle, mapkey) != EFI_SUCCESS)
            Print(L"Error in panic: ExitBootServices() failed.\n");

      } else {

         Print(L"Error in panic: GetMemoryMap() failed.\n");
      }
   }

   disable_interrupts_forced();

   while (true) {
      halt();
   }
}
