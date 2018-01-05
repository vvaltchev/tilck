
/* Usermode application */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdbool.h>


#define FORK_TEST_ITERS (2 * 250 * 1024 * 1024)

void fork_test(void)
{
   printf("Running infinite loop..\n");

   unsigned n = 1;
   int FORK_TEST_ITERS_hits_count = 0;
   bool inchild = false;
   bool exit_on_next_FORK_TEST_ITERS_hit = false;

   while (true) {

      if (!(n % FORK_TEST_ITERS)) {

         printf("[PID: %i] FORK_TEST_ITERS hit!\n", getpid());

         if (exit_on_next_FORK_TEST_ITERS_hit) {
            return;
         }

         FORK_TEST_ITERS_hits_count++;

         if (FORK_TEST_ITERS_hits_count == 1) {

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
               exit_on_next_FORK_TEST_ITERS_hit = true;
            }

         }

         if (FORK_TEST_ITERS_hits_count == 2 && inchild) {
            printf("child: 2 iter hits, exit!\n");
            exit(123);
         }
      }

      n++;
   }
}

int bss_variable[32];

void bss_var_test(void)
{
   for (int i = 0; i < 32; i++) {
      if (bss_variable[i] != 0) {
         printf("%s: FAIL\n", __FUNCTION__);
         exit(1);
      }
   }

   printf("%s: OK\n", __FUNCTION__);
}

void args_test(int argc, char ** argv)
{
   printf("argc: %i\n", argc);

   for (int i = 0; i < argc; i++) {
      printf("argv[%i] = '%s'\n", i, argv[i]);
   }

   printf("env[OSTYPE] = '%s'\n", getenv("OSTYPE"));

   if (strcmp(argv[0], "init")) {
      printf("%s: FAIL\n", __FUNCTION__);
      exit(1);
   }

   if (strcmp(getenv("OSTYPE"), "linux-gnu")) {
      printf("%s: FAIL\n", __FUNCTION__);
      exit(1);
   }
}

int main(int argc, char **argv, char **env)
{
   printf("Hello from init! MY PID IS %i\n", getpid());

   args_test(argc, argv);
   bss_var_test();
   fork_test();

   return 0;
}

