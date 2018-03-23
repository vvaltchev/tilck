
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <exos/term.h>
#include <exos/process.h>

void printk(const char *fmt, ...)
{
   disable_preemption();
   {
      va_list args;
      va_start(args, fmt);
      vprintk(fmt, args);
   }
   enable_preemption();
}
