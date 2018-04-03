
/* Usermode application */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>

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

void file_read_test(void)
{
   char buf[256];
   int fd;

   if (getenv("EXOS")) {
      fd = open("/EFI/BOOT/elf_kernel_stripped", O_RDONLY);
   } else {
      fd = open("build/sysroot/EFI/BOOT/elf_kernel_stripped", O_RDONLY);
   }

   printf("open() = %i\n", fd);

   if (fd < 0) {
      perror("Open failed");
      exit(1);
   }

   int r = read(fd, buf, 256);
   printf("user: read() returned %i\n", r);

   for (int i = 0; i < 16; i++) {
      printf("0x%02x ", (unsigned)buf[i]);
   }

   printf("\n");
   close(fd);
}

int main(int argc, char **argv, char **env)
{
   if (getenv("EXOS")) {
      int in_fd = open("/dev/tty", O_RDONLY);
      int out_fd = open("/dev/tty", O_WRONLY);
      int err_fd = open("/dev/tty", O_WRONLY);
      //printf("in: %i, out: %i, err: %i\n", in_fd, out_fd, err_fd);
      (void)in_fd; (void)out_fd; (void)err_fd;
   }

   printf("Hello from init! MY PID IS %i\n", getpid());

   //args_test(argc, argv);
   //file_read_test();
   //test_read_stdin();

   bss_var_test();

   int shell_pid = fork();

   if (!shell_pid) {
      printf("[init forked child] running shell\n");
      execve("/bin/shell", NULL, NULL);
   }

   printf("[init] wait for the shell to exit\n");
   int wstatus;
   waitpid(shell_pid, &wstatus, 0);
   printf("[init] the shell exited with status: %d\n", WEXITSTATUS(wstatus));

   //fork_test();

   return 0;
}

