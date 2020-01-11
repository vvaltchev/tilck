/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/paging.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/cmdline.h>

/* shared global variables */
const char *cmd_args[MAX_CMD_ARGS] = { "/initrd/bin/init", [1 ... 15] = NULL };
void (*self_test_to_run)(void);
int kopt_tty_count = TTY_COUNT;
bool kopt_serial_console;
bool kopt_sched_alive_thread;

/* static variables */

static enum {

   INITIAL_STATE = 0,
   CUSTOM_START_CMDLINE,
   SET_SELFTEST,
   SET_TTY_COUNT,

   /* --- */
   NUM_ARG_PARSER_STATES

} kernel_arg_parser_state;

static char args_buffer[PAGE_SIZE];
static int last_custom_cmd_n;
static size_t args_buffer_used;

/* code */

static void
parse_arg_state_custom_cmdline(int arg_num, const char *arg, size_t arg_len)
{
   if (last_custom_cmd_n == ARRAY_SIZE(cmd_args) - 1)
      panic("Too many arguments");

   if (args_buffer_used + arg_len + 1 >= sizeof(args_buffer))
      panic("Args too long");

   memcpy(args_buffer + args_buffer_used, arg, arg_len + 1);
   cmd_args[last_custom_cmd_n++] = args_buffer + args_buffer_used;
   args_buffer_used += arg_len + 1;
}

static void
parse_arg_state_set_selftest(int arg_num, const char *arg, size_t arg_len)
{
   char buf[256] = SELFTEST_PREFIX;
   memcpy(buf + sizeof(SELFTEST_PREFIX) - 1, arg, arg_len + 1);

   ulong addr = find_addr_of_symbol(buf);

   if (!addr) {
      printk("*******************************************************\n");
      printk("ERROR: self test function '%s' not found.\n", buf);
      printk("*******************************************************\n");
      return;
   }

   printk("*** Run selftest: '%s' ***\n", arg);
   self_test_to_run = (void *) addr;
}

static void
parse_arg_set_tty_count(int arg_num, const char *arg, size_t arg_len)
{
   if (kopt_serial_console) {
      printk("Ignored -tty_count because of -sercon\n");
      return;
   }

   if (arg[0] < '1' || arg[0] > (MAX_TTYS + '0'))
      panic("Invalid tty_count value '%s'. Expected range: [1, %d].",
            arg, MAX_TTYS);

   kopt_tty_count = arg[0] - '0';
   kernel_arg_parser_state = INITIAL_STATE;
}

static void
parse_arg_state_initial(int arg_num, const char *arg, size_t arg_len)
{
   if (arg_num == 0)
      return;

   if (!strcmp(arg, "-sercon")) {
      kopt_serial_console = true;
      kopt_tty_count = 1;
      return;
   }

   if (!strcmp(arg, "-sat")) {
      kopt_sched_alive_thread = true;
      return;
   }

   if (!strcmp(arg, "-tty_count")) {
      kernel_arg_parser_state = SET_TTY_COUNT;
      return;
   }

   if (!strcmp(arg, "-cmd")) {
      kernel_arg_parser_state = CUSTOM_START_CMDLINE;
      return;
   }

   if (!strcmp(arg, "-s")) {
      kernel_arg_parser_state = SET_SELFTEST;
      return;
   }

   panic("Unrecognized option '%s'", arg);
}

STATIC void use_kernel_arg(int arg_num, const char *arg)
{
   typedef void (*parse_arg_func)(int, const char *, size_t);

   static parse_arg_func table[NUM_ARG_PARSER_STATES] = {
      parse_arg_state_initial,
      parse_arg_state_custom_cmdline,
      parse_arg_state_set_selftest,
      parse_arg_set_tty_count,
   };

   table[kernel_arg_parser_state](arg_num, arg, strlen(arg));
}

static inline void end_arg(char *buf, char **argbuf_ref, int *arg_count_ref)
{
   **argbuf_ref = 0;
   *argbuf_ref = buf;
   use_kernel_arg((*arg_count_ref)++, buf);
}

void parse_kernel_cmdline(const char *cmdline)
{
   char buf[MAX_CMD_ARG_LEN + 1];
   char *argbuf = buf;
   int arg_count = 0;

   for (const char *p = cmdline; *p;) {

      if (*p == ' ') {

         if (argbuf != buf)
            end_arg(buf, &argbuf, &arg_count);

         p++;
         continue;
      }

      if ((argbuf - buf) >= MAX_CMD_ARG_LEN) {

         /* argument truncation: we have no more buffer for this argument */

         end_arg(buf, &argbuf, &arg_count); /* handle the truncated argument */
         while (*p && *p != ' ') p++;       /* skip until the next arg */
         continue;                          /* continue the parsing */
      }

      *argbuf++ = *p++;
   }

   if (argbuf != buf)
      end_arg(buf, &argbuf, &arg_count);
}

