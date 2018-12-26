/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/mman.h>

#include <tilck/common/syscalls.h>
#include "sysenter.h"


#define KB (1024)
#define MB (1024 * 1024)

#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[93m"
#define RESET_ATTRS   "\033[0m"

#define RDTSC() __builtin_ia32_rdtsc()
#define FORK_TEST_ITERS (1 * 250 * 1024 * 1024)

void cmd_loop(int argc, char **argv)
{
   printf("[shell] do a long loop\n");
   for (int i = 0; i < (2 * 1000 * 1000 * 1000); i++) {
      __asm__ volatile ("nop");
   }

   exit(0);
}

void fork_test(int (*fork_func)(void))
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
               printf("[parent] waiting the child to exit...\n");
               int wstatus=0;
               int p = waitpid(pid, &wstatus, 0);
               printf("[parent] child (pid: %i) exited with status: %i!\n",
                      p, WEXITSTATUS(wstatus));
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

   exit(0);
}

void cmd_fork_test(int argc, char **argv)
{
   fork_test(&fork);
}

void cmd_bad_read(int argc, char **argv)
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
   exit(0);
}

void cmd_bad_write(int argc, char **argv)
{
   int ret;
   void *addr = (void *) 0xB0000000;

   errno = 0;
   ret = stat("/", addr);
   printf("ret: %i, errno: %i: %s\n", ret, errno, strerror(errno));
}

void cmd_fork_perf(int argc, char **argv)
{
   const int iters = 150000;
   int wstatus, child_pid;
   unsigned long long start, duration;

   start = RDTSC();

   for (int i = 0; i < iters; i++) {

      child_pid = fork();

      if (child_pid < 0) {
         printf("fork() failed: %s\n", strerror(errno));
         exit(1);
      }

      if (!child_pid) {
         exit(0);
      }

      waitpid(child_pid, &wstatus, 0);
   }


   duration = RDTSC() - start;
   printf("duration: %llu\n", duration/iters);
}

int sysenter_fork(void)
{
   return sysenter_call0(2 /* fork */);
}

void cmd_se_fork_test(int argc, char **argv)
{
   fork_test(&sysenter_fork);
}

void cmd_sysenter(int argc, char **argv)
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
}

void cmd_syscall_perf(int argc, char **argv)
{
   const int iters = 1000;
   unsigned long long start, duration;
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
}

void cmd_fpu(int argc, char **argv)
{
   long double e = 1.0;
   long double f = 1.0;

   for (unsigned i = 1; i < 40; i++) {
      f *= i;
      e += (1.0 / f);
   }

   printf("e(1): %.10Lf\n", e);
}

void cmd_fpu_loop(int argc, char **argv)
{
   register double num = 0;

   for (unsigned i = 0; i < 1000*1000*1000; i++) {

      if (!(i % 1000000))
         printf("%f\n", num);

      num += 1e-6;
   }
}

void cmd_brk_test(int argc, char **argv)
{
   const size_t alloc_size = 1024 * 1024;

   void *orig_brk = (void *)syscall(SYS_brk, 0);
   void *b = orig_brk;

   size_t tot_allocated = 0;

   for (int i = 0; i < 128; i++) {

      void *new_brk = b + alloc_size;

      b = (void *)syscall(SYS_brk, b + alloc_size);

      if (b != new_brk)
         break;

      tot_allocated += alloc_size;
   }

   //printf("tot allocated: %u KB\n", tot_allocated / 1024);

   b = (void *)syscall(SYS_brk, orig_brk);

   if (b != orig_brk) {
      printf("Unable to free mem with brk()\n");
      exit(1);
   }
}

void cmd_mmap_test(int argc, char **argv)
{
   const int iters_count = 10;
   const size_t alloc_size = 1 * MB;
   void *arr[1024];
   int max_mb = -1;

   unsigned long long tot_duration = 0;

   for (int iter = 0; iter < iters_count; iter++) {

      int i;
      unsigned long long start = RDTSC();

      for (i = 0; i < 64; i++) {

         errno = 0;

         void *res = mmap(NULL,
                          alloc_size,
                          PROT_READ | PROT_WRITE,
                          MAP_ANONYMOUS | MAP_PRIVATE,
                          -1,
                          0);

         if (res == (void*) -1) {
            i--;
            break;
         }

         arr[i] = res;
      }

      i--;
      tot_duration += (RDTSC() - start);

      if (max_mb < 0) {

         max_mb = i;

      } else {

         if (i != max_mb) {
            printf("[iter: %u] Unable to alloc max_mb (%u) as previous iters\n",
                   iter, max_mb);

            exit(1);
         }

      }

      printf("[iter: %u][mmap_test] Mapped %u MB\n", iter, i + 1);

      start = RDTSC();

      for (; i >= 0; i--) {

         int rc = munmap(arr[i], alloc_size);

         if (rc != 0) {
            printf("munmap(%p) failed with error: %s\n",
                   arr[i], strerror(errno));
            exit(1);
         }
      }

      tot_duration += (RDTSC() - start);
   }

   printf("\nAvg. cycles for mmap + munmap %u MB: %llu million\n",
          max_mb + 1, (tot_duration / iters_count) / 1000000);
}

void cmd_kernel_cow(int argc, char **argv)
{
   static char cow_buf[4096];

   int wstatus;
   int child_pid = fork();

   if (child_pid < 0) {
      printf("fork() failed\n");
      exit(1);
   }

   if (!child_pid) {
      int rc = stat("/", (void *)cow_buf);
      printf("stat() returned: %d (errno: %s)\n", rc, strerror(errno));
      exit(0);
   }

   waitpid(child_pid, &wstatus, 0);
}

/*
 * Call waitpid() after the child exited.
 */
void cmd_waitpid1(int argc, char **argv)
{
   int wstatus;
   pid_t pid;

   int child_pid = fork();

   if (child_pid < 0) {
      printf("fork() failed\n");
      exit(1);
   }

   if (!child_pid) {
      // This is the child, just exit
      printf("child: exit\n");
      exit(23);
   }

   printf("Created child with pid: %d\n", child_pid);

   /* Wait for the child to exit */
   usleep(100*1000);

   /* Now, let's see call waitpid() after the child exited */
   pid = waitpid(child_pid, &wstatus, 0);

   if (!WIFEXITED(wstatus)) {

      printf("[pid: %d] PARENT: the child %d did NOT exited normally\n",
             getpid(), pid);

      exit(1);
   }

   int exit_code = WEXITSTATUS(wstatus);
   printf("waitpid() returned %d, exit code: %d\n", pid, exit_code);

   if (pid != child_pid) {
      printf("Expected waitpid() to return child's pid (got: %d)\n", pid);
      exit(1);
   }

   if (exit_code != 23) {
      printf("Expected the exit code to be 23 (got: %d)\n", exit_code);
      exit(1);
   }
}

/* waitpid(-1): wait any child to exit */
void cmd_waitpid2(int argc, char **argv)
{
   const int child_count = 3;

   int pids[child_count];
   int wstatus;
   pid_t pid;

   for (int i = 0; i < child_count; i++) {

      int child_pid = fork();

      if (child_pid < 0) {
         printf("fork() failed\n");
         exit(1);
      }

      if (!child_pid) {
         printf("[pid: %d] child: exit (%d)\n", getpid(), 10 + i);
         usleep((child_count - i) * 100*1000);
         exit(10 + i);
      }

      pids[i] = child_pid;
   }

   usleep(120 * 1000);

   for (int i = child_count-1; i >= 0; i--) {

      printf("[pid: %d] PARENT: waitpid(-1)\n", getpid());
      pid = waitpid(-1, &wstatus, 0);

      if (!WIFEXITED(wstatus)) {

         printf("[pid: %d] PARENT: the child %d did NOT exited normally\n",
                 getpid(), pid);

         exit(1);
      }

      int exit_code = WEXITSTATUS(wstatus);

      printf("[pid: %d] PARENT: waitpid() returned %d, exit code: %d\n",
             getpid(), pid, exit_code);

      if (pid != pids[i]) {
         printf("Expected waitpid() to return %d (got: %d)\n", pids[i], pid);
         exit(1);
      }

      if (exit_code != 10+i) {
         printf("Expected the exit code to be %d (got: %d)\n", 10+i, exit_code);
         exit(1);
      }
   }
}

/* wait on any child after they exit */
void cmd_waitpid3(int argc, char **argv)
{
   int wstatus;
   pid_t pid;

   int child_pid = fork();

   if (child_pid < 0) {
      printf("fork() failed\n");
      exit(1);
   }

   if (!child_pid) {
      // This is the child, just exit
      printf("child: exit\n");
      exit(23);
   }

   printf("Created child with pid: %d\n", child_pid);

   /* Wait for the child to exit */
   usleep(100*1000);

   /* Now, let's see call waitpid() after the child exited */
   pid = waitpid(-1, &wstatus, 0);

   if (!WIFEXITED(wstatus)) {

      printf("[pid: %d] PARENT: the child %d did NOT exited normally\n",
             getpid(), pid);

      exit(1);
   }

   int exit_code = WEXITSTATUS(wstatus);
   printf("waitpid() returned %d, exit code: %d\n", pid, exit_code);

   if (pid != child_pid) {
      printf("Expected waitpid() to return child's pid (got: %d)\n", pid);
      exit(1);
   }

   if (exit_code != 23) {
      printf("Expected the exit code to be 23 (got: %d)\n", exit_code);
      exit(1);
   }
}

void cmd_selftest(int argc, char **argv)
{
   if (argc < 1) {
      printf("Expected selftest name argument.\n");
      exit(1);
   }

   int rc =
      sysenter_call3(TILCK_TESTCMD_SYSCALL,
                     TILCK_TESTCMD_RUN_SELFTEST,
                     argv[0]  /* self test name */,
                     NULL);

   if (rc != 0) {
      printf("Invalid selftest '%s'\n", argv[0]);
      exit(1);
   }
}

void cmd_help(int argc, char **argv);

/* ------------------------------------------- */

typedef void (*cmd_func_type)(int argc, char **argv);

enum timeout_type {
   TT_SHORT = 0,
   TT_MED   = 1,
   TT_LONG  = 2,
};

static const char *tt_str[] =
{
   [TT_SHORT] = "tt_short",
   [TT_MED] = "tt_med",
   [TT_LONG] = "tt_long"
};

struct {

   const char *name;
   cmd_func_type fun;
   enum timeout_type tt;
   bool enabled_in_st;

} cmds_table[] = {

   {"help", cmd_help, TT_SHORT, false},
   {"loop", cmd_loop, TT_MED, false},
   {"fork_test", cmd_fork_test, TT_MED, true},
   {"bad_read", cmd_bad_read, TT_SHORT, true},
   {"bad_write", cmd_bad_write, TT_SHORT, true},
   {"fork_perf", cmd_fork_perf, TT_LONG, true},
   {"sysenter", cmd_sysenter, TT_SHORT, true},
   {"syscall_perf", cmd_syscall_perf, TT_SHORT, true},
   {"se_fork_test", cmd_se_fork_test, TT_MED, true},
   {"fpu", cmd_fpu, TT_SHORT, true},
   {"fpu_loop", cmd_fpu_loop, TT_LONG, false},
   {"brk_test", cmd_brk_test, TT_SHORT, true},
   {"mmap_test", cmd_mmap_test, TT_MED, true},
   {"kernel_cow", cmd_kernel_cow, TT_SHORT, true},
   {"waitpid1", cmd_waitpid1, TT_SHORT, true},
   {"waitpid2", cmd_waitpid2, TT_SHORT, true},
   {"waitpid3", cmd_waitpid3, TT_SHORT, true},
   {"selftest", cmd_selftest, TT_LONG, false}
};

void dump_list_of_commands(void)
{
   const int elems = sizeof(cmds_table) / sizeof(cmds_table[0]);

   for (int i = 1; i < elems; i++) {
      if (cmds_table[i].enabled_in_st)
         printf("%s %s\n", cmds_table[i].name, tt_str[cmds_table[i].tt]);
   }

   exit(0);
}

void cmd_help(int argc, char **argv)
{
   printf("\n");
   printf(COLOR_RED "Tilck development shell\n" RESET_ATTRS);

   printf("This application is a small dev-only utility written in ");
   printf("order to allow running\nsimple programs, while proper shells ");
   printf("like ASH can't run on Tilck yet. Behavior:\nif a given command ");
   printf("isn't an executable (e.g. /bin/termtest), it is forwarded ");
   printf("to\n" COLOR_YELLOW "/bin/busybox" RESET_ATTRS);
   printf(". That's how several programs like 'ls' work. Type --help to see\n");
   printf("all the commands built in busybox.\n\n");

   printf(COLOR_RED "Built-in commands\n" RESET_ATTRS);
   printf("    help: shows this help\n");
   printf("    cd <directory>: change the current working directory\n\n");
   printf(COLOR_RED "Kernel tests\n" RESET_ATTRS);

   const int elems = sizeof(cmds_table) / sizeof(cmds_table[0]);
   const int elems_per_row = 7;

   for (int i = 1 /* skip help */; i < elems; i++) {
      printf("%s%s%s ",
             (i % elems_per_row) != 1 ? "" : "    ",
             cmds_table[i].name,
             i != elems-1 ? "," : "");

      if (!(i % elems_per_row))
         printf("\n");
   }

   printf("\n\n");
}

void run_if_known_command(const char *cmd, int argc, char **argv)
{
   const int elems = sizeof(cmds_table) / sizeof(cmds_table[0]);

   for (int i = 0; i < elems; i++) {
      if (!strcmp(cmds_table[i].name, cmd)) {
         cmds_table[i].fun(argc, argv);
         exit(0);
      }
   }
}
