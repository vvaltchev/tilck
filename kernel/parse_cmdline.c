/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/paging.h>
#include <tilck/kernel/elf_utils.h>

const char *cmd_args[16] = { "/sbin/init", [1 ... 15] = NULL };
void (*self_test_to_run)(void);

static enum {

   INITIAL_STATE = 0,
   CUSTOM_START_CMDLINE = 1,

   LAST_STATE

} kernel_arg_parser_state;

static char args_buffer[PAGE_SIZE];
static int last_custom_cmd_n;
static int args_buffer_used;

static void
parse_arg_state_initial(int arg_num, const char *arg, size_t arg_len)
{
   if (!strcmp(arg, "-cmd")) {
      kernel_arg_parser_state = CUSTOM_START_CMDLINE;
      return;
   }

   if (arg_len >= 3 && arg[0] == '-' && arg[1] == 's' && arg[2] == '=') {
      const char *a2 = arg + 3;
      char buf[256] = SELFTEST_PREFIX;

      memcpy(buf + strlen(buf), a2, strlen(a2) + 1);
      uptr addr = find_addr_of_symbol(buf);

      if (!addr) {
         printk("*******************************************************\n");
         printk("ERROR: self test function '%s' not found.\n", buf);
         printk("*******************************************************\n");
         return;
      }

      printk("*** Run selftest: '%s' ***\n", a2);
      self_test_to_run = (void *) addr;
      return;
   }
}

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


static void use_kernel_arg(int arg_num, const char *arg)
{
   typedef void (*parse_arg_func)(int, const char *, size_t);

   static parse_arg_func table[LAST_STATE] = {
      parse_arg_state_initial,
      parse_arg_state_custom_cmdline
   };

   //printk("Kernel arg[%i]: '%s'\n", arg_num, arg);
   table[kernel_arg_parser_state](arg_num, arg, strlen(arg));
}

void parse_kernel_cmdline(const char *cmdline)
{
   char buf[256];
   char *dptr = buf;
   const char *ptr = cmdline;
   int args_count = 0;

   while (*ptr) {

      if (*ptr == ' ' || (dptr-buf >= (sptr)sizeof(buf)-1)) {
         *dptr = 0;
         dptr = buf;
         ptr++;
         use_kernel_arg(args_count++, buf);
         continue;
      }

      *dptr++ = *ptr++;
   }

   if (dptr != buf) {
      *dptr = 0;
      use_kernel_arg(args_count++, buf);
   }
}

