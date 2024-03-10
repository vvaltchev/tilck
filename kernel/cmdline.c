/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_console.h>
#include <tilck_gen_headers/mod_kb8042.h>
#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/paging.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/test/cmdline.h>

const char *cmd_args[MAX_CMD_ARGS] = { "/initrd/bin/init", [1 ... 15] = NULL };
void (*self_test_to_run)(void);


typedef void (*kopt_handler)(const char *arg);

static char args_buf[PAGE_SIZE];
static size_t args_buf_used;
static size_t last_custom_cmd_n;

static bool kopt_is_bool_value(const char *arg)
{
   return !strcmp(arg, "0") || !strcmp(arg, "false") ||
          !strcmp(arg, "1") || !strcmp(arg, "true");
}


static void kopt_handle_bool(bool *var, const char *arg)
{
   if (*arg == '-')
      *var = true;
   else if (!strcmp(arg, "0") || !strcmp(arg, "false"))
      *var = false;
   else if (!strcmp(arg, "1") || !strcmp(arg, "true"))
      *var = true;
   else
      printk("WARNING: Invalid value '%s' for a bool option", arg);
}

static void kopt_handle_long(long *var, const char *arg)
{
   int err = 0;
   long res;

   res = tilck_strtol(arg, NULL, 10, &err);

   if (err) {
      printk("WARNING: failed to parse integer argument '%s'\n", arg);
      return;
   }

   *var = res;
}

static void kopt_handle_ulong(ulong *var, const char *arg)
{
   int base = 10, err = 0;
   ulong res;

   if (arg[0] == '0' && arg[1] == 'x') {
      base = 16;
      arg += 2;
   }

   res = tilck_strtoul(arg, NULL, base, &err);

   if (err) {
      printk("WARNING: failed to parse unsigned argument '%s'\n", arg);
      return;
   }

   *var = res;
}

static void kopt_handle_wordstr(const char **var, const char *arg)
{
   size_t len = strlen(arg);

   if (args_buf_used + len + 1 >= PAGE_SIZE) {
      printk("WARNING: no buffer space for argument: '%s'\n", arg);
      return;
   }

   memcpy(args_buf + args_buf_used, arg, len + 1);
   *var = args_buf + args_buf_used;
   args_buf_used += len + 1;
}

#define DEFINE_KOPT(name, alias, type, default) type kopt_##name = default;
   #include <tilck/common/cmdline_opts.h>
#undef DEFINE_KOPT

#define DEFINE_KOPT KOPT_DEFAULT_ELEM

static const struct kopt all_kopts[] = {
   #include <tilck/common/cmdline_opts.h>
};

#undef DEFINE_KOPT

static void
handle_cmdline_arg(const char *arg)
{
   size_t arg_len = strlen(arg);

   if (last_custom_cmd_n == ARRAY_SIZE(cmd_args) - 1)
      panic("Too many arguments");

   if (args_buf_used + arg_len + 1 >= sizeof(args_buf))
      panic("Args too long");

   memcpy(args_buf + args_buf_used, arg, arg_len + 1);
   cmd_args[last_custom_cmd_n++] = args_buf + args_buf_used;
   args_buf_used += arg_len + 1;
}

static void
handle_selftest_kopt(void)
{
   char buf[MAX_CMD_ARG_LEN + 1] = SELFTEST_PREFIX;
   ulong addr;

   if (!kopt_selftest)
      return;

   strcat(buf, kopt_selftest);
   buf[sizeof(buf) - 1] = 0; /* Always truncate the buffer */
   addr = find_addr_of_symbol(buf);

   if (!addr) {
      printk("*******************************************************\n");
      printk("ERROR: self test function '%s' not found.\n", buf);
      printk("*******************************************************\n");
      return;
   }

   printk("*** Run selftest: '%s' ***\n", kopt_selftest);
   self_test_to_run = (void *) addr;
}

enum arg_state {
   INITIAL_STATE,
   WAITING_FOR_VALUE,
   FINAL_STATE_CMDLINE,
};

struct arg_parse_ctx {
   enum arg_state state;
   const struct kopt *last_opt;
};

static void
handle_arg_generic(const struct kopt *opt, const char *arg)
{
   switch (opt->type) {

      case KOPT_TYPE_bool:
         kopt_handle_bool(opt->data, arg);
         break;

      case KOPT_TYPE_long:
         kopt_handle_long(opt->data, arg);
         break;

      case KOPT_TYPE_ulong:
         kopt_handle_ulong(opt->data, arg);
         break;

      case KOPT_TYPE_wordstr:
         kopt_handle_wordstr(opt->data, arg);
         break;

      default:
         NOT_REACHED();
   }
}

STATIC void
use_kernel_arg(struct arg_parse_ctx *ctx, int arg_num, const char *arg)
{
   const struct kopt *opt;
   u32 i;

   switch (ctx->state) {

      case FINAL_STATE_CMDLINE:
         handle_cmdline_arg(arg);
         break;

      case WAITING_FOR_VALUE:
         handle_arg_generic(ctx->last_opt, arg);
         ctx->state = INITIAL_STATE;
         break;

      case INITIAL_STATE:
      {
         if (kopt_is_bool_value(arg)) {

            /*
             * Even if we're in the initial state, check for bool values
             * because boolean options accept a value optionally. For example,
             * "-pk" means flips its default configuration from false to true.
             * However, the explicit form "-pk true" is also support, as well
             * as the form "-pk false" which has no effect, when the default
             * value is already false.
             */

            if (ctx->last_opt) {

               if (ctx->last_opt->type == KOPT_TYPE_bool) {
                  handle_arg_generic(ctx->last_opt, arg);
                  ctx->last_opt = NULL;
               } else {
                  printk("WARNING: Unexpected bool value '%s'\n", arg);
               }

            } else {
               printk("WARNING: Unexpected naked bool value '%s'\n", arg);
            }

            break;
         }

         if (!strcmp(arg, "-cmd") || !strcmp(arg, "--")) {
            ctx->state = FINAL_STATE_CMDLINE;
            break;
         }

         for (i = 0; i < ARRAY_SIZE(all_kopts); i++) {

            if (*arg != '-')
               continue;

            if (!strcmp(arg+1, all_kopts[i].name))
               break;

            if (all_kopts[i].alias[0] != '\0')
               if (!strcmp(arg+1, all_kopts[i].alias))
                  break;
         }

         if (i == ARRAY_SIZE(all_kopts)) {

            if (arg_num > 0 || *arg == '-')
               printk("WARNING: Unrecognized cmdline option '%s'\n", arg);

            break;
         }

         opt = &all_kopts[i];
         ctx->last_opt = opt;

         if (opt->type == KOPT_TYPE_bool) {
            handle_arg_generic(opt, arg);
         } else {
            ctx->state = WAITING_FOR_VALUE;
         }

         break;
      }
   }
}

static void
do_args_validation(void)
{
   if (kopt_sercon) {

      if (kopt_ttys != TTY_COUNT) {
         printk("WARNING: Ignored -ttys because of -sercon\n");
      }

      kopt_ttys = 1;
   }

   if (kopt_ttys <= 0 || kopt_ttys > MAX_TTYS) {

      printk("WARNING: Invalid value '%ld' for ttys. Expected range: [1, %d]\n",
             kopt_ttys, MAX_TTYS);

      kopt_ttys = TTY_COUNT;
   }

   handle_selftest_kopt();
}

static inline void
end_arg(struct arg_parse_ctx *ctx,
        char *buf,
        char **argbuf_ref,
        int *arg_count_ref)
{
   **argbuf_ref = 0;
   *argbuf_ref = buf;
   use_kernel_arg(ctx, (*arg_count_ref)++, buf);
}

static void debug_check_all_kopts(void)
{
   for (u32 i = 0; i < ARRAY_SIZE(all_kopts); i++) {
      for (u32 j = 0; j < ARRAY_SIZE(all_kopts); j++) {

         if (i <= j) {

            /*
             * Avoid comparing twice the same pair and skip the comparison
             * between an element and itself.
             */
            continue;
         }

         const char *ni = all_kopts[i].name;
         const char *nj = all_kopts[j].name;
         const char *ai = all_kopts[i].alias;
         const char *aj = all_kopts[j].alias;

         if (!strcmp(ni, nj) ||
             (aj[0] && !strcmp(ni, aj)) ||
             (ai[0] && !strcmp(ai, nj)) ||
             (ai[0] && aj[0] && !strcmp(ai, aj)))
         {
            panic("Name conflict between kopt[%u] and kopt[%u]", i, j);
         }
      }

      if (!strcmp(all_kopts[i].name, "cmd") ||
          !strcmp(all_kopts[i].alias, "cmd"))
      {
         panic("Cannot use 'cmd' as name or alias for kopt[%u]", i);
      }
   }
}

void parse_kernel_cmdline(const char *cmdline)
{
   char buf[MAX_CMD_ARG_LEN + 1];
   char *argbuf = buf;
   int arg_count = 0;
   struct arg_parse_ctx ctx = {0};

#ifndef UNIT_TEST_ENVIRONMENT
   printk("Kernel cmdline: '%s'\n", cmdline);
#endif

   DEBUG_ONLY(debug_check_all_kopts());

   for (const char *p = cmdline; *p;) {

      if (*p == ' ') {

         if (argbuf != buf)
            end_arg(&ctx, buf, &argbuf, &arg_count);

         p++;
         continue;
      }

      if ((argbuf - buf) >= MAX_CMD_ARG_LEN) {

         /* argument truncation: we have no more buffer for this argument */
         end_arg(&ctx, buf, &argbuf, &arg_count); /* handle the trunc. arg */
         while (*p && *p != ' ') p++;             /* skip until the next arg */
         continue;                                /* continue the parsing */
      }

      *argbuf++ = *p++;
   }

   if (argbuf != buf)
      end_arg(&ctx, buf, &argbuf, &arg_count);

   do_args_validation();
}
