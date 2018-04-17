
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define RDTSC() __builtin_ia32_rdtsc()
#define FORK_TEST_ITERS (2 * 250 * 1024 * 1024)

void cmd_loop(void)
{
   printf("[shell] do a long loop\n");
   for (int i = 0; i < 500*1000*1000; i++) {
      __asm__ volatile ("nop");
   }

   exit(0);
}

void cmd_fork_test(void)
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

            int pid = fork();

            printf("Fork returned %i\n", pid);

            if (pid == 0) {
               printf("############## I'm the child!\n");
               inchild = true;
            } else {
               printf("############## I'm the parent, child's pid = %i\n", pid);
               printf("[parent] waiting the child to exit...\n");
               int wstatus=0;
               int p = waitpid(pid, &wstatus, 0);
               printf("[parent] child (pid: %i) exited with status: %i!\n", p, WEXITSTATUS(wstatus));
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

void cmd_invalid_read(void)
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

void cmd_invalid_write(void)
{
   int ret;
   void *addr = (void *) 0xB0000000;
   int child_pid;

   child_pid = fork();

   if (!child_pid) {
      printf("child: press enter\n");
      getchar();
      exit(0);
   }

   printf("waitpid with invalid wstatus ptr\n");
   ret = waitpid(child_pid, addr, 0);
   printf("ret: %i, errno: %i: %s\n", ret, errno, strerror(errno));

   printf("read from stdin into an invalid user buffer:\n");

   errno = 0;
   ret = read(0, addr, 32);
   printf("ret: %i, errno: %i: %s\n", ret, errno, strerror(errno));
}

void cmd_fork_perf(void)
{
   const int iters = 200000;
   int wstatus, child_pid;
   unsigned long long start, duration;

   start = RDTSC();

   for (int i = 0; i < iters; i++) {

      child_pid = fork();

      if (!child_pid) {
         exit(0);
      }

      waitpid(child_pid, &wstatus, 0);
   }


   duration = RDTSC() - start;
   printf("duration: %llu\n", duration/iters);
}



int do_sysenter_call(int syscall, void *arg1, void *arg2, void *arg3)
{
   int ret;

   __asm__ volatile ("pushl $ret\n\t"
                     "pushl %%ecx\n\t"
                     "pushl %%edx\n\t"
                     "pushl %%ebp\n\t"
                     "movl %%esp, %%ebp\n\t"
                     "sysenter\n\t"
                     "ret:\n\t"
                     : "=a" (ret)
                     : "a" (syscall), "b" (arg1), "c" (arg2), "d" (arg3)
                     : "memory", "cc");

   return ret;
}

#define sysenter_call(n, a1, a2, a3) \
   do_sysenter_call((n), (void*)(a1), (void*)(a2), (void*)(a3))

void cmd_sysenter(void)
{
   const char *str = "hello from a sysenter call!\n";
   size_t len = strlen(str);

   int ret = sysenter_call(4  /* write */,
                           1  /* stdout */,
                           str,
                           len);

   printf("The syscall returned: %i\n", ret);
}

/* ------------------------------------------- */

typedef void (*cmd_func_type)(void);

struct {

   const char *name;
   cmd_func_type fun;

} cmds_table[] = {

   {"loop", cmd_loop},
   {"fork_test", cmd_fork_test},
   {"invalid_read", cmd_invalid_read},
   {"invalid_write", cmd_invalid_write},
   {"fork_perf", cmd_fork_perf},
   {"sysenter", cmd_sysenter}
};

void run_if_known_command(const char *cmd)
{
   const int elems = sizeof(cmds_table) / sizeof(cmds_table[0]);

   for (int i = 0; i < elems; i++) {
      if (!strcmp(cmds_table[i].name, cmd)) {
         cmds_table[i].fun();
         exit(0);
      }
   }
}
