
#include <debug_utils.h>
#include <string_util.h>
#include <arch/generic_x86/utils.h>

size_t stackwalk32(void **frames, size_t count)
{
   void *retAddr;
   void *ebp;
   size_t i;

   ebp = (void *) (&frames - 2);

   for (i = 0; i < count; i++) {

      retAddr = *((void **)ebp + 1);
      ebp = *(void **)ebp;

      if (!ebp || !retAddr) {
         break;
      }

      frames[i] = retAddr;
   }

   return i;
}

size_t stackwalk32_ex(void *ebp, void **frames, size_t count)
{
   void *retAddr;
   size_t i;

   for (i = 0; i < count; i++) {

      retAddr = *((void **)ebp + 1);
      ebp = *(void **)ebp;

      if (!ebp || !retAddr) {
         break;
      }

      frames[i] = retAddr;
   }

   return i;
}


void dump_stacktrace()
{
   void *frames[10] = {0};
   size_t c = stackwalk32(frames, 10);

   printk("*** STACKTRACE ***\n");

   for (size_t i = 0; i < c; i++) {
      printk("frame[%i]: %p\n", i, frames[i]);
   }

   printk("\n\n");
}

void dump_stacktrace_ex(void *ebp)
{
   void *frames[10] = {0};
   size_t c = stackwalk32_ex(ebp, frames, 10);

   printk("*** STACKTRACE ***\n");

   for (size_t i = 0; i < c; i++) {
      printk("frame[%i]: %p\n", i, frames[i]);
   }

   printk("\n\n");
}

#ifdef __i386__

#include <arch/i386/paging_int.h>

int debug_count_used_pdir_entries(page_directory_t *pdir)
{
   int used = 0;
   for (int i = 0; i < 1024; i++) {
      used += (pdir->page_tables[i] != NULL);
   }
   return used;
}

void debug_dump_used_pdir_entries(page_directory_t *pdir)
{
   printk("Used pdir entries:\n");

   for (int i = 0; i < 1024; i++) {
      if (pdir->page_tables[i] != NULL) {
         printk("Index: %i (= vaddr %p)\n", i, (uptr)i << 22);
      }
   }
}

#endif

volatile bool in_panic = false;

void panic(const char *fmt, ...)
{
   if (in_panic) {
      return;
   }

   in_panic = true;

   //cli();

   printk("\n\n************** KERNEL PANIC **************\n");

   va_list args;
   va_start(args, fmt);
   vprintk(fmt, args);
   va_end(args);

   printk("\n");

   dump_stacktrace();
   //sti();

   while (true) {
      halt();
   }
}

void assert_failed(const char *expr, const char *file, int line)
{
   panic("\nASSERTION '%s' FAILED in file '%s' at line %i\n",
         expr, file, line);
}


