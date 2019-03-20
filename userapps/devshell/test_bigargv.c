/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "devshell.h"
#include "sysenter.h"

static void do_bigargv_test(size_t len)
{
   char *big_arg = malloc(len + 1);
   memset(big_arg, 'a', len);
   big_arg[len] = 0;
   char *argv[] = { shell_argv[0], big_arg, NULL };

   close(0); close(1); close(2);

   execve(argv[0], argv, shell_env);

   /* If we got here, execve() failed */
   if (errno == E2BIG)
      exit(123);

   exit(99); /* unexpected case */
}

static bool fails_with_e2big(size_t len)
{
   int rc;
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
   DEVSHELL_CMD_ASSERT(!fails_with_e2big(3000));
   DEVSHELL_CMD_ASSERT(fails_with_e2big(4096));
   printf("[PASS]\n");
   return 0;
}
