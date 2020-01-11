/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "devshell.h"
#include "sysenter.h"

bool running_on_tilck(void)
{
   return getenv("TILCK") != NULL;
}

void not_on_tilck_message(void)
{
   fprintf(stderr, "[SKIP]: Test designed to run exclusively on Tilck\n");
}


int cmd_loop(int argc, char **argv)
{
   printf(PFX "Do a long NOP loop\n");
   for (int i = 0; i < (2 * 1000 * 1000 * 1000); i++) {
      asmVolatile("nop");
   }

   return 0;
}

int cmd_bad_read(int argc, char **argv)
{
   int ret;
   void *addr = (void *) 0xB0000000;
   printf("[cmd] req. kernel to read unaccessibile user addr: %p\n", addr);

   /* write to stdout a buffer unaccessibile for the user */
   errno = 0;
   ret = write(1, addr, 16);
   printf("ret: %i, errno: %i: %s\n", ret, errno, strerror(errno));

   addr = (void *) 0xC0000000;
   printf("[cmd] req. kernel to read unaccessible user addr: %p\n", addr);

   /* write to stdout a buffer unaccessibile for the user */
   errno = 0;
   ret = write(1, addr, 16);
   printf("ret: %i, errno: %i: %s\n", ret, errno, strerror(errno));

   printf("Open with filename invalid ptr\n");
   ret = open((char*)0xB0000000, 0);

   printf("ret: %i, errno: %i: %s\n", ret, errno, strerror(errno));
   return 0;
}

int cmd_bad_write(int argc, char **argv)
{
   int ret;
   void *addr = (void *) 0xB0000000;

   errno = 0;
   ret = stat("/", addr);
   printf("ret: %i, errno: %i: %s\n", ret, errno, strerror(errno));
   return 0;
}

int cmd_sysenter(int argc, char **argv)
{
   const char *str = "hello from a sysenter call!\n";
   size_t len = strlen(str);

   int ret = sysenter_call3(4  /* write */,
                            1  /* stdout */,
                            str,
                            len);

   printf("The syscall returned: %i\n", ret);
   printf("sleep (int 0x80)..\n");
   usleep(100*1000);
   printf("after sleep, everything is fine.\n");
   printf("same sleep, but with sysenter:\n");

   struct timespec req = { .tv_sec = 0, .tv_nsec = 100*1000*1000 };
   sysenter_call3(162 /* nanosleep */, &req, NULL, NULL);
   printf("after sleep, everything is fine. Prev ret: %i\n", ret);
   return 0;
}

int cmd_syscall_perf(int argc, char **argv)
{
   const int iters = 1000;
   ull_t start, duration;
   pid_t uid = getuid();

   start = RDTSC();

   for (int i = 0; i < iters; i++) {
      __asm__ ("int $0x80"
               : /* no output */
               : "a" (23),          /* 23 = sys_setuid */
                 "b" (uid)
               : /* no clobber */);
   }

   duration = RDTSC() - start;

   printf("int 0x80 setuid(): %llu cycles\n", duration/iters);

   start = RDTSC();

   for (int i = 0; i < iters; i++)
      sysenter_call1(23 /* setuid */, uid /* uid */);

   duration = RDTSC() - start;

   printf("sysenter setuid(): %llu cycles\n", duration/iters);
   return 0;
}

int cmd_fpu(int argc, char **argv)
{
   long double e = 1.0;
   long double f = 1.0;

   for (unsigned i = 1; i < 40; i++) {
      f *= i;
      e += (1.0 / f);
   }

   printf("e(1): %.10Lf\n", e);
   return 0;
}

int cmd_fpu_loop(int argc, char **argv)
{
   register double num = 0;

   for (unsigned i = 0; i < 1000*1000*1000; i++) {

      if (!(i % 1000000))
         printf("%f\n", num);

      num += 1e-6;
   }

   return 0;
}

/*
 * Test the scenario where a user copy-on-write happens in the kernel because
 * of a syscall.
 */
int cmd_kcow(int argc, char **argv)
{
   static char cow_buf[4096];

   int wstatus;
   int child_pid = fork();

   if (child_pid < 0) {
      printf("fork() failed\n");
      return 1;
   }

   if (!child_pid) {

      int rc = stat("/", (void *)cow_buf);

      if (rc != 0) {
         printf("stat() failed with %d: %s [%d]\n", rc, strerror(errno), errno);
         exit(1);
      }

      exit(0); // exit from the child
   }

   waitpid(child_pid, &wstatus, 0);
   return 0;
}


static int cloexec_do_exec(int argc, char **argv)
{
   int rc =
      fprintf(stderr,
              COLOR_RED "[execve-proc] stderr works [it should NOT!]"
              RESET_ATTRS "\n");

   if (rc < 0) {
      printf("[execve-proc] write to stderr failed, AS EXPECTED\n");
      return 0;
   }

   return 1;
}

int cmd_cloexec(int argc, char **argv)
{
   int pid;
   int wstatus;
   const char *devshell_path = get_devshell_path();

   if (argc > 0) {

      if (!strcmp(argv[0], "do_exec"))
         return cloexec_do_exec(argc, argv);

      printf(PFX "[cloexec] Invalid sub-command '%s'\n", argv[0]);
      return 1;
   }

   pid = fork();

   if (pid < 0) {
      perror("fork() failed");
      exit(1);
   }

   if (!pid) {
      char *argv[] = { "devshell", "-c", "cloexec", "do_exec", NULL };

      int flags = fcntl(2 /* stderr */, F_GETFD);
      int rc = fcntl(2 /* stderr */, F_SETFD, flags | FD_CLOEXEC);

      if (rc < 0) {
         perror("fcntl() failed");
         exit(1);
      }

      fprintf(stderr, "[forked-child] Stderr works [expected to work]\n");
      execvpe(devshell_path, argv, shell_env);
      fprintf(stderr, "execvpe('%s') failed\n", devshell_path);
      exit(1);
   }

   waitpid(pid, &wstatus, 0);

   if (!WIFEXITED(wstatus)) {
      printf("Test child killed by signal: %s\n", strsignal(WTERMSIG(wstatus)));
      exit(1);
   }

   return WEXITSTATUS(wstatus);
}
