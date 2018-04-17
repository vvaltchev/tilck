
#include <exos/kmalloc.h>

char *strdup(const char *s)
{
   if (!s)
      return NULL;

   size_t len = strlen(s) + 1;
   char *copy = kmalloc(len);

   if (!copy)
      return NULL;

   memcpy(copy, s, len);
   return copy;
}

char *const *dup_strarray(const char *const *argv)
{
   int argc = 0;
   char **res;

   if (!argv)
      return NULL;

   while (argv[argc])
      argc++;

   res = kmalloc(sizeof(uptr) * (argc + 1));

   if (!res)
      return NULL;

   for (int i = 0; i < argc; i++) {

      res[i] = strdup(argv[i]);

      if (!res[i]) {
         for (i--; i >= 0; i--)
            kfree(res[i]);
         return NULL;
      }
   }

   res[argc] = NULL;
   return res;
}

void free_strarray(char *const *argv)
{
   char *const *p = argv;

   if (!argv)
      return;

   while (*p)
      kfree(*p++);

   kfree((void *) argv);
}
