
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/debug_utils.h>

bool no_init;
void (*self_test_to_run)(void);
const char *cmd_args[16] = { "/sbin/init", [1 ... 15] = NULL };

void use_kernel_arg(int arg_num, const char *arg)
{
   static char args_buffer[PAGE_SIZE];
   static bool in_custom_cmd;
   static int custom_cmd_arg;
   static int args_buffer_used;

   //printk("Kernel arg[%i]: '%s'\n", arg_num, arg);

   const size_t arg_len = strlen(arg);

   if (in_custom_cmd) {

      if (custom_cmd_arg == ARRAY_SIZE(cmd_args) - 1)
         panic("Too many arguments");

      if (args_buffer_used + arg_len + 1 >= sizeof(args_buffer))
         panic("Args too long");

      memcpy(args_buffer + args_buffer_used, arg, arg_len + 1);
      cmd_args[custom_cmd_arg++] = args_buffer + args_buffer_used;
      args_buffer_used += arg_len + 1;
      return;
   }

   if (!strcmp(arg, "-noinit")) {
      no_init = true;
      return;
   }

   if (!strcmp(arg, "-cmd")) {
      in_custom_cmd = true;
      return;
   }

   if (arg_len >= 3) {
      if (arg[0] == '-' && arg[1] == 's' && arg[2] == '=') {
         const char *a2 = arg + 3;
         char buf[256] = "selftest_";

         printk("*** Run selftest: '%s' ***\n", a2);

         memcpy(buf+strlen(buf), a2, strlen(a2) + 1);
         uptr addr = find_addr_of_symbol(buf);

         if (!addr) {
            panic("Self test function '%s' not found.\n", buf);
         }

         self_test_to_run = (void (*)(void)) addr;
         return;
      }
   }
}

void parse_kernel_cmdline(const char *cmdline)
{
   char buf[256];
   char *dptr = buf;
   const char *ptr = cmdline;
   int args_count = 0;

   while (*ptr) {

      if (*ptr == '+' || *ptr == ' ' || (dptr-buf >= (sptr)sizeof(buf)-1)) {
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

