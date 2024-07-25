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
   int ret, err;
   void *addr = (void *) 0xB0000000;
   printf("[cmd] req. kernel to read unaccessibile user addr: %p\n", addr);

   /* write to stdout a buffer unaccessibile for the user */
   errno = 0;
   ret = write(1, addr, 16);
   err = errno;
   printf("ret: %i, errno: %i: %s\n", ret, err, strerror(err));
   DEVSHELL_CMD_ASSERT(err == EFAULT);

   addr = (void *) 0xC0000000;
   printf("[cmd] req. kernel to read unaccessible user addr: %p\n", addr);

   /* write to stdout a buffer unaccessibile for the user */
   errno = 0;
   ret = write(1, addr, 16);
   err = errno;
   printf("ret: %i, errno: %i: %s\n", ret, err, strerror(err));
   DEVSHELL_CMD_ASSERT(err == EFAULT);

   printf("Open with filename invalid ptr\n");

   errno = 0;
   ret = open((char*)0xB0000000, 0);
   err = errno;
   printf("ret: %i, errno: %i: %s\n", ret, errno, strerror(errno));
   DEVSHELL_CMD_ASSERT(err == EFAULT);
   return 0;
}

int cmd_bad_write(int argc, char **argv)
{
   int ret, err;
   void *addr = (void *) 0xB0000000;

   errno = 0;
   ret = syscall(SYS_gettimeofday, addr, NULL);
   err = errno;
   printf("ret: %i, errno: %i: %s\n", ret, err, strerror(err));
   DEVSHELL_CMD_ASSERT(err == EFAULT);
   return 0;
}

int cmd_sysenter(int argc, char **argv)
{
   const char *str = "hello from a sysenter call!\n";
   size_t len = strlen(str);

   int ret = sysenter_call3(SYS_write  /* write */,
                            1  /* stdout */,
                            str,
                            len);

   printf("The syscall returned: %i\n", ret);
   printf("sleep (int 0x80)..\n");
   usleep(100*1000);
   printf("after sleep, everything is fine.\n");
   printf("same sleep, but with sysenter:\n");

   struct timespec req = { .tv_sec = 0, .tv_nsec = 100*1000*1000 };

#ifdef __i386__
   ASSERT(SYS_nanosleep == 162);
#endif

   sysenter_call3(SYS_nanosleep /* nanosleep_time32 */, &req, NULL, NULL);
   printf("after sleep, everything is fine. Prev ret: %i\n", ret);
   return 0;
}

int cmd_syscall_perf(int argc, char **argv)
{
   const int major_iters = 100;
   const int iters = 1000;
   ull_t start, duration;
   ull_t best = (ull_t) -1;

   for (int j = 0; j < major_iters; j++) {

      start = RDTSC();

      for (int i = 0; i < iters; i++)
         syscall(SYS_getuid);

      duration = RDTSC() - start;

      if (duration < best)
         best = duration;
   }

   printf("int 0x80 getuid(): %llu cycles\n", best/iters);
   best = (ull_t) -1;

   for (int j = 0; j < major_iters; j++) {

      start = RDTSC();

      for (int i = 0; i < iters; i++)
         sysenter_call0(SYS_getuid);

      duration = RDTSC() - start;

      if (duration < best)
         best = duration;
   }

   printf("sysenter getuid(): %llu cycles\n", best/iters);
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

/* Test scripts testing EXTRA components running on Tilck */

static const char *extra_test_scripts[] = {
   "tcc",
   "tar",
   "sysfs",
};

static int run_extra_test(const char *name)
{
   int rc, pid, wstatus;
   char buf[64];

   printf("%s Extra: %s\n", STR_RUN, name);
   sprintf(buf, "/initrd/usr/local/tests/%s", name);

   pid = vfork();
   DEVSHELL_CMD_ASSERT(pid >= 0);

   if (!pid) {
      rc = execl(buf, buf, NULL);
      perror("Execve failed");
      _exit(1);
   }

   rc = waitpid(pid, &wstatus, 0);
   DEVSHELL_CMD_ASSERT(rc == pid);
   rc = WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0;

   printf("%s Extra: %s\n", rc ? STR_PASS : STR_FAIL, name);
   return !rc;
}

int cmd_extra(int argc, char **argv)
{
   int rc = 0;
   struct stat statbuf;

   if (!getenv("TILCK")) {
      printf(PFX "[SKIP] because we're not running on Tilck\n");
      return 0;
   }

   if (stat("/bin/ash", &statbuf) < 0) {
      printf(PFX "[SKIP] because busybox is not present\n");
      return 0;
   }

   for (int i = 0; i < ARRAY_SIZE(extra_test_scripts); i++) {
      if ((rc = run_extra_test(extra_test_scripts[i])))
         break;
   }

   return rc;
}

int cmd_getuids(int argc, char **argv)
{
   DEVSHELL_CMD_ASSERT(syscall(SYS_getuid) == 0);
   DEVSHELL_CMD_ASSERT(syscall(SYS_getgid) == 0);
   DEVSHELL_CMD_ASSERT(syscall(SYS_geteuid) == 0);

   #ifdef __i386__
      DEVSHELL_CMD_ASSERT(syscall(SYS_getuid16) == 0);
      DEVSHELL_CMD_ASSERT(syscall(SYS_getgid16) == 0);
      DEVSHELL_CMD_ASSERT(syscall(SYS_geteuid16) == 0);
      DEVSHELL_CMD_ASSERT(syscall(SYS_getegid16) == 0);
   #endif

   return 0;
}

int cmd_exit_cb(int argc, char **argv)
{
   int wstatus;
   int child_pid;
   long before_cb;
   long after_cb;
   int rc;

   rc = sysenter_call3(TILCK_CMD_SYSCALL,
                       TILCK_CMD_GET_VAR_LONG,
                       "test_on_exit_cb_counter",
                       &before_cb);

   if(rc == -ENOENT) {
      printf(PFX "[SKIP] No kernel symbols\n");
      return 0;
   }

   DEVSHELL_CMD_ASSERT(before_cb == 3);
   child_pid = fork();

   if (child_pid < 0) {
      fprintf(stderr, "%s\n", "fork() failed");
      return 1;
   }

   if (!child_pid) {

      int ret = sysenter_call2(TILCK_CMD_SYSCALL,
                               TILCK_CMD_CALL_FUNC_0,
                               "register_test_on_exit_callback");

      exit(ret == -ENOENT ? 99 : ret);
   }

   waitpid(child_pid, &wstatus, 0);
   DEVSHELL_CMD_ASSERT(WEXITSTATUS(wstatus) == 0);

   sysenter_call3(TILCK_CMD_SYSCALL,
                  TILCK_CMD_GET_VAR_LONG,
                  "test_on_exit_cb_counter",
                  &after_cb);

   sysenter_call2(TILCK_CMD_SYSCALL,
                  TILCK_CMD_CALL_FUNC_0,
                  "unregister_test_on_exit_callback");

   DEVSHELL_CMD_ASSERT(after_cb == before_cb + 1);
   return 0;
}

static bool
comes_after(struct timeval t2,
            struct timeval t1)
{
   // t1 must come before t2
   return t2.tv_sec > t1.tv_sec
       || (t2.tv_sec == t1.tv_sec
         && t2.tv_usec > t1.tv_usec);
}

static void busy_wait(void)
{
   // Arbitrary amount of work
   for (int i = 0; i < 10000; i++)
      asmVolatile("nop"); // Don't let the compiler optimize this loop
}

static void busy_wait_kernel(void)
{
   int rc = syscall(TILCK_CMD_SYSCALL, TILCK_CMD_BUSY_WAIT, 10000);
   DEVSHELL_CMD_ASSERT(!rc); /* TILCK_CMD_BUSY_WAIT must never fail */
}

int cmd_getrusage(int argc, char **argv)
{
   int rc, n, max;
   struct rusage buf_0;
   struct rusage buf_1;

   // Try RUSAGE_SELF
   rc = getrusage(RUSAGE_SELF, &buf_0);
   DEVSHELL_CMD_ASSERT(rc == 0);

   // Check that the reported times increase as expected.
   // Do some work in both user and kernel space and then
   // check that the reported time increased relative to
   // the first call to "getrusage". If the busy wait didn't
   // trigger an increment, try again. The test fails on
   // timeout or the number of attempts reaches a given
   // threshold.
   {
      max = 50000;

      busy_wait();
      busy_wait_kernel();

      rc = getrusage(RUSAGE_SELF, &buf_1);
      DEVSHELL_CMD_ASSERT(rc == 0);

      // Note that user and system time are increased
      // indipendantly to do the least amount of busy
      // wait.

      for (n = 0; !comes_after(buf_1.ru_utime, buf_0.ru_utime); n++) {

         busy_wait();

         rc = getrusage(RUSAGE_SELF, &buf_1);
         DEVSHELL_CMD_ASSERT(rc == 0);
         DEVSHELL_CMD_ASSERT(n < max);
      }

      for (n = 0; !comes_after(buf_1.ru_stime, buf_0.ru_stime); n++) {

         busy_wait_kernel();

         rc = getrusage(RUSAGE_SELF, &buf_1);
         DEVSHELL_CMD_ASSERT(rc == 0);
         DEVSHELL_CMD_ASSERT(n < max);
      }
   }

   // Try RUSAGE_THREAD
   rc = getrusage(RUSAGE_THREAD, &buf_1);
   DEVSHELL_CMD_ASSERT(rc == 0);

   // Now check that RUSAGE_CHILDREN doesn't work.
   // At the moment it's not implemented so for it to work
   // there bust be a bug!
   rc = getrusage(RUSAGE_CHILDREN, &buf_0);
   DEVSHELL_CMD_ASSERT(rc == -1 && errno == EINVAL);

   // Check failure when the "who" argument is invalid.
   int bad_who = 1000;
   rc = getrusage(bad_who, &buf_0);
   DEVSHELL_CMD_ASSERT(rc == -1 && errno == EINVAL);

   // Check failure when the provided buffer pointer
   // doesn't refer to an accessible page
   void *bad_ptr = NULL;
   rc = getrusage(RUSAGE_SELF, bad_ptr);
   DEVSHELL_CMD_ASSERT(rc == -1 && errno == EFAULT);

   printf("OK\n");
   return 0;
}
