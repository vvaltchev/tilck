
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/debug_utils.h>

bool no_init;
void (*self_test_to_run)(void);

void use_kernel_arg(int arg_num, const char *arg)
{
   //printk("Kernel arg[%i]: '%s'\n", arg_num, arg);

   const size_t arg_len = strlen(arg);

   if (!strcmp(arg, "-noinit")) {
      no_init = true;
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
