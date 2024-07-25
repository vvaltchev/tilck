/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/mman.h>

#include "devshell.h"
#include "sysenter.h"

static int sysenter_fork(void)
{
#if defined(__i386__) || defined(__x86_64__)
   return sysenter_call0(SYS_fork /* fork */);
#elif defined(__riscv)
   return fork();
#endif
}

static int fork_test(int (*fork_func)(void))
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
            break;
         }

         FORK_TEST_ITERS_hits_count++;

         if (FORK_TEST_ITERS_hits_count == 1) {

            printf("forking..\n");

            int pid = fork_func();

            printf("Fork returned %i\n", pid);

            if (pid == 0) {
               printf("############## I'm the child!\n");
               inchild = true;
            } else {
               printf("############## I'm the parent, child's pid = %i\n", pid);
               printf(STR_PARENT "waiting the child to exit...\n");
               int wstatus=0;
               int p = waitpid(pid, &wstatus, 0);
               printf(STR_PARENT "child (pid: %i) exited with status: %i!\n",
                      p, WEXITSTATUS(wstatus));
               exit_on_next_FORK_TEST_ITERS_hit = true;
            }

         }

         if (FORK_TEST_ITERS_hits_count == 2 && inchild) {
            printf("child: 2 iter hits, exit!\n");
            exit(123); // exit from the child
         }
      }

      n++;
   }

   return 0;
}

int cmd_fork0(int argc, char **argv)
{
   return fork_test(&fork);
}

static int do_fork_perf(int (*fork_func)(void))
{
   const int iters = 150000;
   int rc, wstatus, child_pid;
   ull_t start, duration;

   start = RDTSC();

   for (int i = 0; i < iters; i++) {

      child_pid = fork_func();

      if (child_pid < 0) {
         perror("fork() failed");
         return 1;
      }

      if (!child_pid)
         exit(0); // exit from the child

      rc = waitpid(child_pid, &wstatus, 0);

      if (rc < 0) {
         perror("waitpid() failed");
         return 1;
      }

      if (rc != child_pid) {
         printf("waitpid() returned %d [expected: %d]\n", rc, child_pid);
         return 1;
      }
   }

   duration = RDTSC() - start;
   printf("duration: %llu\n", duration/iters);
   return 0;
}

int cmd_fork_se(int argc, char **argv)
{
   return fork_test(&sysenter_fork);
}

int cmd_fork_perf(int argc, char **argv)
{
   return do_fork_perf(&fork);
}

int cmd_vfork_perf(int argc, char **argv)
{
   return do_fork_perf(&vfork);
}

int cmd_execve0(int argc, char **argv)
{
   int rc, pid, wstatus;
   char buf[32];
   const char *devshell_path = get_devshell_path();

   if (argc >= 2 && !strcmp(argv[0], "--test1")) {

      printf("[execve child] Trying to access mapped mem before fork\n");
      ulong val = strtoul(argv[1], NULL, 16);
      void *ptr = (void *)val;
      strcpy(ptr, "hello world\n");
      printf("[execve child] Something's wrong: I succeeded!\n");
      return 0;
   }

   printf(STR_PARENT "Calling fork()...\n");
   pid = fork();
   DEVSHELL_CMD_ASSERT(pid >= 0);

   if (!pid) {

      printf(STR_CHILD "alloc 1 MB with mmap()\n");

      void *res = mmap(NULL,
                       1 * MB,
                       PROT_READ | PROT_WRITE,
                       MAP_ANONYMOUS | MAP_PRIVATE,
                       -1,
                       0);

      DEVSHELL_CMD_ASSERT(res != (void *)-1);
      strcpy(res, "I can write here, for sure!");

      sprintf(buf, "%p", res);
      printf(STR_CHILD "call execve(devshell)\n");
      execl(devshell_path, "devshell", "-c", "execve0", "--test1", buf, NULL);
      perror("execl");
      exit(123);
   }

   printf(STR_PARENT "Wait for child, expecting it killed by SIGSEGV\n");

   rc = waitpid(pid, &wstatus, 0);
   DEVSHELL_CMD_ASSERT(rc == pid);
   print_waitpid_change(pid, wstatus);

   DEVSHELL_CMD_ASSERT(WIFSIGNALED(wstatus));
   DEVSHELL_CMD_ASSERT(WTERMSIG(wstatus) == SIGSEGV);
   return 0;
}

int cmd_fork1(int argc, char **argv)
{
   int rc, pid, wstatus;
   void *mmap_addr;

   printf(STR_CHILD "alloc 1 MB with mmap()\n");

   mmap_addr = mmap(NULL,
                    1 * MB,
                    PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_PRIVATE,
                    -1,
                    0);

   DEVSHELL_CMD_ASSERT(mmap_addr != (void *)-1);

   strcpy(mmap_addr, "hello from parent");
   printf(STR_PARENT "Calling fork()...\n");

   pid = fork();
   DEVSHELL_CMD_ASSERT(pid >= 0);

   if (!pid) {

      printf(STR_CHILD "hello from the child\n");
      printf(STR_CHILD "mmap_addr: '%s'\n", (char *)mmap_addr);
      printf(STR_CHILD "Calling munmap()..\n");

      if ((rc = munmap(mmap_addr, 1 * MB))) {
         perror("munmap failed in the child");
         exit(1);
      }

      printf(STR_CHILD "clean exit\n");
      exit(0);
   }

   printf(STR_PARENT "Wait for the child\n");
   rc = waitpid(pid, &wstatus, 0);
   DEVSHELL_CMD_ASSERT(rc == pid);
   print_waitpid_change(pid, wstatus);

   DEVSHELL_CMD_ASSERT(!WIFSIGNALED(wstatus));
   DEVSHELL_CMD_ASSERT(WEXITSTATUS(wstatus) == 0);

   if ((rc = munmap(mmap_addr, 1 * MB)))
      perror("munmap failed in the parent");

   printf(STR_PARENT "clean exit\n");
   return 0;
}

int cmd_vfork0(int argc, char **argv)
{
   static const char child_hello[] = "Hello from the child!!";
   static const char parent_hello[] = "Hello from the parent!!";

   int rc, pid, wstatus, failed = 0;
   volatile int stack_var = 0;       /* changed by child: must be volatile */
   void *volatile mmap_addr = NULL;  /* changed by child: must be volatile */

   printf(STR_PARENT "Call vfork()..\n");
   pid = vfork();
   DEVSHELL_CMD_ASSERT(pid >= 0);

   if (!pid) {

      printf(STR_CHILD "In vfork-ed child..\n");
      printf(STR_CHILD "mmap(1 MB)...\n");

      mmap_addr = mmap(NULL,
                       1 * MB,
                       PROT_READ | PROT_WRITE,
                       MAP_ANONYMOUS | MAP_PRIVATE,
                       -1,
                       0);

      printf(STR_CHILD "mmap_addr: %p\n", mmap_addr);
      DEVSHELL_CMD_ASSERT(mmap_addr != (void *)-1);
      forced_memcpy(mmap_addr, child_hello, sizeof(child_hello));

      printf(STR_CHILD "Sleep a little\n");
      usleep(70 * 1000);

      printf(STR_CHILD "Setting stack_var\n");
      stack_var = 12345;

      printf(STR_CHILD "Call _exit(0)\n");
      _exit(0);
   }

   if (stack_var != 12345) {
      printf(STR_PARENT "ERROR: stack_var not set by vforked-child!\n");
      failed = 1;
   }

   printf(STR_PARENT "Read from mmap_addr:\n");
   printf(STR_PARENT "'%s'\n", (char *)mmap_addr);

   printf(STR_PARENT "Write to mmap_addr...\n");
   forced_memcpy(mmap_addr, parent_hello, sizeof(parent_hello));

   rc = waitpid(pid, &wstatus, 0);
   DEVSHELL_CMD_ASSERT(rc == pid);
   print_waitpid_change(pid, wstatus);
   return failed;
}
