
#include <common_defs.h>
#include <string_util.h>
#include <arch/generic_x86/x86_utils.h>

void memmove(void *dest, const void *src, size_t n)
{
   for (size_t i = 0; i < n; i++) {
      ((char*)dest)[i] = ((char*)src)[i];
   }
}

void memcpy(void *dest, const void *src, size_t n)
{
   return memmove(dest, src, n);
}

NORETURN void panic(const char *fmt, ...)
{
   printk("\n********************* BOOTLOADER PANIC *********************\n");

   va_list args;
   va_start(args, fmt);
   vprintk(fmt, args);
   va_end(args);

   printk("\n");

   while (true) {
      halt();
   }
}

void enable_preemption() { }
void disable_preemption() { }

