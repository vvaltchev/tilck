
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

char *const *dcopy_strarray(const char *const *argv)
{
   int argc = 0;
   char **res;

   if (!argv)
      return NULL;

   while (argv[argc])
      argc++;

   res = kmalloc(sizeof(uptr) * (argc + 1));
   VERIFY(res != NULL);

   for (int i = 0; i < argc; i++) {
      res[i] = strdup(argv[i]);
      VERIFY(res[i] != NULL);
   }

   res[argc + 1] = NULL;
   return res;
}

void dfree_strarray(char *const *argv)
{
   char *const *p = argv;

   if (!argv)
      return;

   while (*p)
      kfree(*p++);

   kfree((void *) argv);
}
