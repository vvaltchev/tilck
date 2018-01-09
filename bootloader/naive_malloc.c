
#include <common_defs.h>

static size_t allocated = 0;
static char buffer[1024] = {0};

void *kmalloc(size_t n)
{
   if (allocated + n >= sizeof(buffer))
      return NULL;

   void *res = buffer + allocated;
   allocated += n;

   return res;
}

void kfree(void *ptr, size_t n)
{
   /* Do nothing. This is a trivial alloc-only malloc implementation. */
}
