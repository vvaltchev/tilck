/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <linux/reboot.h>

#include <tilck_gen_headers/mod_console.h>
#include <tilck_gen_headers/config_init.h>

#include <tilck/common/basic_defs.h> /* for MIN() and ARRAY_SIZE() */

static char *start_script_args[2] = { START_SCRIPT, NULL };
static char *shell_args[16] = { DEFAULT_SHELL, [1 ... 15] = NULL };
static int shell_pids[128] = {0};
static volatile bool in_shutdown;
static int video_tty_count;
static FILE *tty0;
static int reboot_cmd;

static int fork_and_run_shell_on_tty(int tty);

/* -- command line options -- */

static bool opt_quiet;
static bool opt_nostart;
static bool opt_no_shell_respawn;
static bool opt_do_exit;

/* -- end -- */

NORETURN static void
call_exit(int code)
{
   printf("[init] exit with code: %i\n", code);
   exit(code);
}

static void
do_graceful_shutdown(void)
{
   in_shutdown = true;
   printf("[init] Sending SIGTERM to all tasks...\n");
   kill(-1, SIGTERM);
   printf("[init] Waiting at most 1 sec...\n");
}

static void
init_handle_sig_term(int sig)
{
   printf("[init] Reboot...\n");
   do_graceful_shutdown();
   reboot_cmd = LINUX_REBOOT_CMD_RESTART;
}

static void
init_handle_sig_usr1(int sig)
{
   printf("[init] Halt...\n");
   do_graceful_shutdown();
   reboot_cmd = LINUX_REBOOT_CMD_HALT;
}

static void
init_handle_sig_usr2(int sig)
{
   printf("[init] Poweroff...\n");
   do_graceful_shutdown();
   reboot_cmd = LINUX_REBOOT_CMD_POWER_OFF;
}

static void
init_set_signal_mask(void)
{
   signal(SIGTERM, &init_handle_sig_term);
   signal(SIGUSR1, &init_handle_sig_usr1);
   signal(SIGUSR2, &init_handle_sig_usr2);
}

static void
init_reset_signal_mask(void)
{
   signal(SIGTERM, SIG_DFL);
   signal(SIGUSR1, SIG_DFL);
   signal(SIGUSR2, SIG_DFL);
}

static int get_video_tty_count(void)
{
   static int count = 0;
   struct stat statbuf;
   char buf[32];

   if (!getenv("TILCK")) {
      /* On Linux we're not going to use multiple TTYs for testing. */
      return 1;
   }

   if (count)
      return count;

   for (int i = 0; i <= MAX_TTYS; i++) {

      sprintf(buf, "/dev/tty%d", i);

      if (stat(buf, &statbuf) != 0)
         break;

      if (i > 0)
         count++; /* don't count /dev/tty0 */
   }

   return count;
}

static void open_std_handles(int tty)
{
   char ttyfile[32] = "/dev/console";

   if (tty >= 0) {

      if (tty < TTYS0_MINOR)
         sprintf(ttyfile, "/dev/tty%d", tty);
      else
         sprintf(ttyfile, "/dev/ttyS%d", tty - TTYS0_MINOR);
   }

   int in = open(ttyfile, O_RDONLY);
   int out = open(ttyfile, O_WRONLY);
   int err = open(ttyfile, O_WRONLY);

   if (in != 0) {
      printf("[init] in: %i, expected: 0\n", in);
      call_exit(1);
   }

   if (out != 1) {
      printf("[init] out: %i, expected: 1\n", out);
      call_exit(1);
   }

   if (err != 2) {
      printf("[init] err: %i, expected: 2\n", err);
      call_exit(1);
   }
}

static void run_start_script(void)
{
   int rc, wstatus;
   struct stat statbuf;
   pid_t pid;

   rc = stat(BUSYBOX, &statbuf);

   if (rc < 0 || (statbuf.st_mode & S_IFMT) != S_IFREG) {
      printf("[init][WARNING] Unable to run the script %s: "
             "busybox not found\n", START_SCRIPT);
      return;
   }

   pid = fork();

   if (pid < 0) {
      printf("[init] fork() failed with %d\n", pid);
      call_exit(1);
   }

   if (!pid) {

      init_reset_signal_mask();
      execve(start_script_args[0], start_script_args, NULL);
      printf("[init] execve(%s) failed with: %s\n",
             start_script_args[0],
             strerror(errno));
      exit(1);
   }

   rc = waitpid(-1, &wstatus, 0);

   if (rc != pid) {
      printf("[init] waitpid(-1) returned %d instead of pid %d\n", rc, pid);
      call_exit(1);
   }

   if (!WIFEXITED(wstatus)) {
      printf("[init] Start script killed by sig %d\n", WTERMSIG(wstatus));
      call_exit(1);
   }

   if (WEXITSTATUS(wstatus) != 0) {
      printf("[init] Start script exited with %d\n", WEXITSTATUS(wstatus));
      call_exit(1);
   }
}

static void do_initial_setup(void)
{
   if (!getenv("TILCK")) {

      fprintf(stderr, "[init] Tilck specific program. Not tested on Linux\n");
      exit(1);

      // if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) < 0)
      //    perror("prctl(PR_SET_CHILD_SUBREAPER) failed");
   }

   open_std_handles(-1);
   tty0 = fopen("/dev/tty0", "w");

   if (!tty0) {
      fprintf(stderr, "[init] Unable to open /dev/tty0: %s\n", strerror(errno));
      exit(1);
   }

   /*
    * In order to allow the start /etc/start to begin simply with #!/bin/sh
    * instead of "#!/initrd/bin/busybox ash", we need to create /bin and symlink
    * busybox to /bin/sh.
    */

   if (mkdir("/bin", 0777) < 0) {
      fprintf(stderr, "[init] Failed to create /bin: %s\n", strerror(errno));
      exit(1);
   }

   if (symlink("/initrd/bin/busybox", "/bin/sh") < 0) {
      fprintf(stderr, "[init] Failed to create symlink: %s\n", strerror(errno));
      exit(1);
   }
}

static int get_tty_for_shell_pid(pid_t shell_pid)
{
   for (int i = 0; i < ARRAY_SIZE(shell_pids); i++) {

      if (shell_pids[i] == shell_pid)
         return i;
   }

   return -1;
}

static void report_shell_exit(pid_t pid, int tty, int wstatus)
{
   const int status = WEXITSTATUS(wstatus);
   const int term_sig = WTERMSIG(wstatus);

   if (term_sig) {
      fprintf(tty0, "[init] the shell with pid %d was killed by signal %d\n",
              pid, term_sig);
   } else {
      fprintf(tty0, "[init] the shell with pid %d exited with status: %d\n",
              pid, status);
   }

   if (opt_no_shell_respawn || in_shutdown)
      return;

   fprintf(tty0, "\n");
   shell_pids[tty] = fork_and_run_shell_on_tty(tty);
}

static void report_process_exit(pid_t pid, int wstatus)
{
   if (opt_quiet || !INIT_REPORT_PROC_EXIT)
      return;

   const int status = WEXITSTATUS(wstatus);
   const int term_sig = WTERMSIG(wstatus);

   if (term_sig)
      printf("[init] process %d killed by signal: %d\n", pid, term_sig);
   else
      printf("[init] process %d exited with status: %d\n", pid, status);
}

static void show_help_and_exit(void)
{
   printf("\nUsage:\n\n");
   printf("    init                Regular run. Shell: %s\n", DEFAULT_SHELL);
   printf("    init -h/--help      Show this help and exit\n");
   printf("    init -q             Quiet: don't report exit of orphan tasks\n");
   printf("    init -ns            Don't run the script: %s\n", START_SCRIPT);
   printf("    init -nr            Don't respawn shells\n");
   printf("    init -- <cmdline>   "
          "Run the specified cmdline instead of the default one.\n");
   printf("                        "
          "Example: init -- /bin/devshell -c waitpid2\n");

   exit(0);
}

static void parse_opts(int argc, char **argv)
{
begin:

   if (!argc)
      return;

   if (!strcmp(*argv, "-q")) {
      opt_quiet = true;
      argc--; argv++;
      goto begin;
   }

   if (!strcmp(*argv, "-ns")) {
      opt_nostart = true;
      argc--; argv++;
      goto begin;
   }

   if (!strcmp(*argv, "-nr")) {
      opt_no_shell_respawn = true;
      argc--; argv++;
      goto begin;
   }

   if (!strcmp(*argv, "-e")) {
      opt_do_exit = true;
      argc--; argv++;
      goto begin;
   }

   if (!strcmp(*argv, "--")) {
      const int elems = MIN(ARRAY_SIZE(shell_args), argc - 1);
      memcpy(shell_args, argv + 1, elems * sizeof(char *));
      return;
   }

   /* Unknown option case */
   printf("[init] Unknown option '%s'\n", *argv);
}

static void wait_for_children(int timeout_ms)
{
   const bool was_in_shutdown = in_shutdown;
   int shell_tty, wstatus;
   int elapsed_ms = timeout_ms ? 0 : -1;
   pid_t pid;

   while (elapsed_ms < timeout_ms) {

      if (!timeout_ms) {

         pid = waitpid(-1, &wstatus, 0);

      } else {

         pid = waitpid(-1, &wstatus, WNOHANG);

         if (pid == 0) {

            /* no child changed its state */
            usleep(100 * 1000); /* 100 ms */
            elapsed_ms += 100;
            continue;
         }
      }

      if (pid < 0) {

         if (errno == EINTR) {

            if (was_in_shutdown != in_shutdown) {
               /* Graceful shutdown started */
               break;
            }

            continue;
         }

         if (errno == ECHILD)
            break;

         printf("[init] waitpid() returned: %d (%s)\n", errno, strerror(errno));
      }

      shell_tty = get_tty_for_shell_pid(pid);

      if (shell_tty > 0)
         report_shell_exit(pid, shell_tty, wstatus);
      else
         report_process_exit(pid, wstatus);
   }
}

static void setup_console_for_shell(int tty)
{
   if (tty == 1 || (tty == TTYS0_MINOR && !video_tty_count)) {

      /*
       * For /dev/tty1 we're already fine. The current process already has tty1
       * as controlling terminal (set by the kernel) and do_initial_setup()
       * called open_std_handles(1). For the other shell instances instead, we
       * have to correctly setup the tty.
       */
      return;
   }

   close(2);
   close(1);
   close(0);

   setsid();                 /* Reset the controlling terminal */
   open_std_handles(tty);    /* Open /dev/ttyN as stdin, stdout and stderr */
   ioctl(0, TIOCSCTTY, 0);   /* Make ttyN to be the controlling terminal */
   tcsetpgrp(0, getpgid(0)); /* Make this pgid the foreground process group */
}

static void tilck_console_msg_loop(int tty)
{
   char buf[32];
   int poll_timeout = -1;                    /* infinite */
   int rc;

   struct pollfd fd = {
      .fd = 0,
      .events = POLLIN,
   };

   do {

      const char *serial_tty_suffix = "";
      int user_tty_num = tty;

      if (tty >= TTYS0_MINOR) {

         poll_timeout = 3000;                /* 3 seconds */
         serial_tty_suffix = "S";
         user_tty_num = tty - TTYS0_MINOR;

         /* clear the screen */
         printf("\033[2J\033[1;1H");
      }

      printf("Tilck console on /dev/tty%s%d\n",
               serial_tty_suffix,
               user_tty_num);

      printf("------------------------------------------\n\n");
      printf("Press ENTER to run the shell");

      fflush(stdout);

   } while (poll(&fd, 1, poll_timeout) <= 0);

   /* drain the input buffer */
   do {

      rc = read(0, buf, sizeof(buf));

      if (rc < 0 && errno == EINTR)
         continue;

   } while (rc == sizeof(buf));

   printf("\n");
}

static void getty_handle_sigterm(int sig)
{
   /* clear the screen */
   printf("\033[2J\033[1;1H");

   /* write a meaningful message on the screen */
   printf("System shutdown.\n");

   /* graceful exit */
   exit(0);
}

static int fork_and_run_shell_on_tty(int tty)
{
   static bool already_run;

   pid_t pid = fork();

   if (pid < 0) {
      perror("[init] fork() failed");
      call_exit(1);
   }

   if (pid) {
      /* Parent process */
      already_run = true;
      return pid;
   }

   /* Child process */
   setup_console_for_shell(tty);
   init_reset_signal_mask();
   signal(SIGTERM, &getty_handle_sigterm);

   if (video_tty_count && (tty > 1 || already_run)) {

      /*
       * Instead of wasting resources for running the shell from now, just wait
       * until an user is connected and presses ENTER.
       */

      tilck_console_msg_loop(tty);
   }

   execve(shell_args[0], shell_args, NULL);

   printf("[init] execve(%s) failed with: %s\n",
          shell_args[0],
          strerror(errno));

   call_exit(1);
}

int main(int argc, char **argv, char **env)
{
   int pid = getpid();
   struct stat statbuf;

   if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))
      show_help_and_exit();

   if (pid != 1) {

      fprintf(stderr,
              "FATAL ERROR: init has pid %d.\n"
              "Init expects to be the 1st user process (pid 1) "
              "to run on the system.\n"
              "Running it on an already running system is *not* supported.\n",
              pid);

      return 1;
   }

   video_tty_count = get_video_tty_count();

   init_set_signal_mask();
   do_initial_setup();
   parse_opts(argc - 1, argv + 1);

   if (!opt_nostart) {
      run_start_script();
   } else {
      printf("[init] Skipping the start script\n");
   }

   for (int tty = 1; tty <= video_tty_count; tty++) {

      pid = fork_and_run_shell_on_tty(tty);
      shell_pids[tty] = pid;

      if (tty == 1) {
         /* only the 1st shell gets executed with the specified arguments */
         for (int i = 1; i < ARRAY_SIZE(shell_args); i++)
            shell_args[i] = NULL;
      }
   }

   if (!video_tty_count || SERIAL_CON_IN_VIDEO_MODE) {
      if (!stat("/dev/ttyS0", &statbuf)) {
         pid = fork_and_run_shell_on_tty(TTYS0_MINOR);
         shell_pids[TTYS0_MINOR] = pid;
      }
   }

   wait_for_children(0);

   if (opt_do_exit) {

      /*
       * This option is used by automated system tests run on QEMU.
       * In that context, init is expected to just exit after all of its
       * children died. That triggers a call to debug_qemu_turn_off_machine(),
       * instead of a regular kernel panic.
       */
      exit(0);
   }

   wait_for_children(1000);

   printf("[init] Sync...\n");
   sync();
   printf("[init] Sending SIGKILL to all tasks...\n");
   kill(-1, SIGKILL);

   wait_for_children(1000);

   if (reboot(reboot_cmd)) {
      /* Restart, halt or power-off failed */
      exit(2);
   }
}

