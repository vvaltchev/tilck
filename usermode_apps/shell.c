
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv, char **env)
{
   printf("Hello from my simple shell!\n");

   printf("env:\n");

   while (*env)
      printf("%s\n", *env++);

   printf("\n");
   printf("args (%d):\n", argc);

   for (int i = 0; i < argc; i++)
      printf("argv[%i] = '%s'\n", i, argv[i]);
   printf("\n");

   char buf[256];
   char *res;

   res = getcwd(buf, sizeof(buf));

   if (res != buf) {
      perror("Shell: getcwd failed");
      return 1;
   }

   printf("CWD: '%s'\n", buf);

   chdir("/testdir");

   res = getcwd(buf, sizeof(buf));

   if (res != buf) {
      perror("Shell: getcwd failed");
      return 1;
   }

   printf("CWD: '%s'\n", buf);

   return 0;
}
