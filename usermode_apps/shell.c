
#include <stdio.h>

int main(int argc, char **argv, char **env)
{
   printf("Hello from my simple shell!\n");

   printf("env:\n");
   while (*env) {
      printf("%s\n", *env);
      env++;
   }

   printf("args (%d):\n", argc);
   for (int i = 0; i < argc; i++)
      printf("argv[%i] = '%s'\n", i, argv[i]);
   printf("\n");
   return 88;
}
