/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>

#include "devshell.h"
#include "sysenter.h"
#include "test_common.h"

static void do_bigargv_test(size_t len)
{
   char *big_arg = malloc(len);
   char *argv[] = { DEVSHELL_PATH, big_arg, NULL };

   memset(big_arg, 'a', len);
   big_arg[len-1] = 0;

   close(0); close(1); close(2);
   execvpe(DEVSHELL_PATH, argv, shell_env);

   /* If we got here, execve() failed */
   if (errno == E2BIG)
      exit(123);

   exit(99); /* unexpected case */
}

static bool fails_with_e2big(size_t len)
{
   int pid;
   int wstatus;

   pid = fork();

   if (pid < 0) {
      perror("fork() failed");
      exit(1);
   }

   if (!pid)
      do_bigargv_test(len);

   waitpid(pid, &wstatus, 0);

   if (!WIFEXITED(wstatus)) {
      printf("Test child killed by signal: %s\n", strsignal(WTERMSIG(wstatus)));
      exit(1);
   }

   if (WEXITSTATUS(wstatus) == 99) {
      printf("execve() in the child failed with something != E2BIG\n");
      exit(1);
   }

   /*
    * At this point, only two options left:
    *    - execve() failed with E2BIG and the child exited with our special 123
    *    - execve() did NOT fail and executed the program with either failed
    *      or succeeded, but anyway exited with something different than 123.
    */
   return WEXITSTATUS(wstatus) == 123;
}

int cmd_bigargv(int argc, char **argv)
{
   size_t l = 1024;
   size_t r = 16 * 4096;
   size_t a0;
   size_t argv_len = 0, env_len = 0;

   if (!running_on_tilck()) {
      not_on_tilck_message();
      return 0;
   }

   DEVSHELL_CMD_ASSERT(!fails_with_e2big(l));
   DEVSHELL_CMD_ASSERT(fails_with_e2big(r));

   while (l < r) {

      size_t v = (l + r) / 2;

      if (fails_with_e2big(v)) {
         r = v;
      } else {
         l = v + 1;
      }
   }

   size_t v = l - 1;

   assert(!fails_with_e2big(v));
   assert(fails_with_e2big(v + 1));

   a0 = strlen(DEVSHELL_PATH) + 1;

   for (char **e = shell_env; *e; e++) {
      env_len += strlen(*e) + 1 + sizeof(void *);
   }

   env_len += 1 * sizeof(void *); // +1 for the final NULL
   argv_len = 3 * sizeof(void *); // +3 pointer size integers [a0,a1,NULL]
   argv_len += a0;                // len(argv[0]) + 1
   argv_len += v;                 // max argv[1] length

   size_t grand_tot = argv_len + env_len;
   size_t expected_tot = USER_ARGS_PAGE_COUNT * getpagesize();

   printf("fix argv[0] length: %zu\n", a0);
   printf("max argv[1] length: %zu\n", v);
   printf("tot argv    length: %zu\n", argv_len);
   printf("tot env     length: %zu\n", env_len);
   printf("grand_tot         : %zu\n", grand_tot);
   printf("expected_tot      : %zu\n", expected_tot);

   DEVSHELL_CMD_ASSERT(grand_tot == expected_tot);
   return 0;
}
