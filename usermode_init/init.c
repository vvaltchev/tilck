
/* Usermode application */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

typedef unsigned char bool;
#define true 1
#define false 0

int bss_variable[32];

#define ITERS (250 * 1024 * 1024)

int main(int argc, char **argv, char **env)
{
   printf("### Hello from init! MY PID IS %i\n", getpid());

   printf("argc: %i\n", argc);

   for (int i = 0; i < argc; i++) {
      printf("argv[%i] = '%s'\n", i, argv[i]);
   }

   printf("env[OSTYPE] = '%s'\n", getenv("OSTYPE"));


   int stackVar;
   printf("&stackVar = %p\n", &stackVar);

   for (int i = 0; i < 4; i++) {
      printf("BssVar[%i] = %i\n", i, bss_variable[i]);
   }

   int ret = open("/myfile.txt", 0xAABB, 0x112233);

   printf("ret = %i\n", ret);

   for (int i = 0; i < 5; i++) {
      printf("i = %i\n", i);
   }

   printf("Running infinite loop..\n");

   unsigned n = 1;
   int iters_hits_count = 0;
   bool inchild = false;
   bool exit_on_next_iters_hit = false;

   while (true) {

      if (!(n % ITERS)) {

         printf("[PID: %i] iters hit!\n", getpid());

         if (exit_on_next_iters_hit) {
            return 0;
         }

         iters_hits_count++;

         if (iters_hits_count == 1) {

            printf("forking..\n");

            int pid = fork();

            printf("Fork returned %i\n", pid);

            if (pid == 0) {
               printf("############## I'm the child!\n");
               inchild = true;
            } else {
               printf("############## I'm the parent, child's pid = %i\n", pid);
               printf("[parent] waiting the child to exit...\n");
               int p = waitpid(pid, NULL, 0);
               printf("[parent] child (pid: %i) exited!\n", p);
               exit_on_next_iters_hit = true;
            }

         }

         if (iters_hits_count == 2 && inchild) {
            printf("child: 2 iter hits, exit!\n");
            exit(123);
         }
      }

      n++;
   }
}

