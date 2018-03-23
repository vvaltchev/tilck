
#include <common_defs.h>
#include <string_util.h>

char small_heap[4 * KB];
size_t heap_used;

void *kmalloc(size_t n)
{
   if (heap_used + n >= sizeof(small_heap)) {
      panic("kmalloc: unable to allocate %u bytes!", n);
   }

   void *result = small_heap + heap_used;
   heap_used += n;

   return result;
}

void kfree2(void *ptr, size_t n)
{
   /* DO NOTHING */
}

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
      asmVolatile("hlt");
   }
}

// Hack to make the linker happy (printk() in string_utils needs it).
u32 disable_preemption_count = 1;
