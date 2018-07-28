
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

#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[93m"
#define RESET_ATTRS   "\033[0m"

#define RDTSC() __builtin_ia32_rdtsc()
#define FORK_TEST_ITERS (1 * 250 * 1024 * 1024)

void cmd_loop(void)
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

void cmd_fork_test(void)
{
   fork_test(&fork);
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

   errno = 0;
   ret = stat("/", addr);
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

int do_sysenter_call0(int syscall)
{
   int ret;

   __asm__ volatile ("pushl $1f\n\t"
                     "pushl %%ecx\n\t"
                     "pushl %%edx\n\t"
                     "pushl %%ebp\n\t"
                     "movl %%esp, %%ebp\n\t"
                     "sysenter\n\t"
                     "1:\n\t"
                     : "=a" (ret)
                     : "a" (syscall)
                     : "memory", "cc");

   return ret;
}

int do_sysenter_call1(int syscall, void *arg1)
{
   int ret;

   __asm__ volatile ("pushl $1f\n\t"
                     "pushl %%ecx\n\t"
                     "pushl %%edx\n\t"
                     "pushl %%ebp\n\t"
                     "movl %%esp, %%ebp\n\t"
                     "sysenter\n\t"
                     "1:\n\t"
                     : "=a" (ret)
                     : "a" (syscall), "b" (arg1)
                     : "memory", "cc");

   return ret;
}

int do_sysenter_call3(int syscall, void *arg1, void *arg2, void *arg3)
{
   int ret;

   __asm__ volatile ("pushl $1f\n\t"
                     "pushl %%ecx\n\t"
                     "pushl %%edx\n\t"
                     "pushl %%ebp\n\t"
                     "movl %%esp, %%ebp\n\t"
                     "sysenter\n\t"
                     "1:\n\t"
                     : "=a" (ret)
                     : "a" (syscall), "b" (arg1), "c" (arg2), "d" (arg3)
                     : "memory", "cc");

   return ret;
}

#define sysenter_call0(n) \
   do_sysenter_call0((n))

#define sysenter_call1(n, a1) \
   do_sysenter_call1((n), (void*)(a1))

#define sysenter_call3(n, a1, a2, a3) \
   do_sysenter_call3((n), (void*)(a1), (void*)(a2), (void*)(a3))

int sysenter_fork(void)
{
   return sysenter_call0(2 /* fork */);
}

void cmd_sysenter_fork_test(void)
{
   fork_test(&sysenter_fork);
}

void cmd_sysenter(void)
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
   sysenter_call3(162 /* nanosleep */, NULL, NULL, NULL);
   printf("after sleep, everything is fine. Prev ret: %i\n", ret);
}

void cmd_syscall_perf(void)
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

void cmd_fpu(void)
{
   double num;

   printf("Enter real number: ");
   fflush(stdout);

   fscanf(stdin, "%lf", &num);
   printf("n^2 = %lf\n", num * 2.0);
}

void cmd_fpu_loop(void)
{
   register double num = 0;

   for (unsigned i = 0; i < 1000*1000*1000; i++) {

      if (!(i % 1000000))
         printf("%f\n", num);

      num += 1e-6;
   }
}


void cmd_brk_test(void)
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

void cmd_help(void);

/* ------------------------------------------- */

typedef void (*cmd_func_type)(void);

struct {

   const char *name;
   cmd_func_type fun;

} cmds_table[] = {

   {"help", cmd_help},
   {"loop", cmd_loop},
   {"fork_test", cmd_fork_test},
   {"invalid_read", cmd_invalid_read},
   {"invalid_write", cmd_invalid_write},
   {"fork_perf", cmd_fork_perf},
   {"sysenter", cmd_sysenter},
   {"syscall_perf", cmd_syscall_perf},
   {"sysenter_fork_test", cmd_sysenter_fork_test},
   {"fpu", cmd_fpu},
   {"fpu_loop", cmd_fpu_loop},
   {"brk_test", cmd_brk_test}
};

void cmd_help(void)
{
   printf("\n");
   printf(COLOR_RED "Tilck development shell\n" RESET_ATTRS);

   printf("This application is a small dev-only utility written in ");
   printf("order to allow running\nsimple programs, while proper shells ");
   printf("like ASH can't run on Tilck yet. Behavior:\nif a given coomand ");
   printf("isn't an executable (e.g. /bin/termtest), it is forwarded ");
   printf("to\n" COLOR_YELLOW "/bin/busybox" RESET_ATTRS);
   printf(". That's how several programs like 'ls' work. Type --help to see\n");
   printf("all the commands built in busybox.\n\n");

   printf(COLOR_RED "Built-in commands\n" RESET_ATTRS);
   printf("    help: shows this help\n");
   printf("    cd <directory>: change the current working directory\n\n");
   printf(COLOR_RED "Kernel tests\n" RESET_ATTRS);

   const int elems = sizeof(cmds_table) / sizeof(cmds_table[0]);
   const int elems_per_row = 6;

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
