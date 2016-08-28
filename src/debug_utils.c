
#include <debug_utils.h>
#include <stringUtil.h>

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
         printk("Index: %i (= paddr %p)\n", i, (uintptr_t)i << 22);
      }
   }
}
