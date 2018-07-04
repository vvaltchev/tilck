
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>

NORETURN void panic(const char *fmt, ...)
{
   printk("\n********************* BOOTLOADER PANIC *********************\n");

   va_list args;
   va_start(args, fmt);
   vprintk(fmt, args);
   va_end(args);

   printk("\n");

   while (true) {
      asmVolatile("hlt");
   }
}
