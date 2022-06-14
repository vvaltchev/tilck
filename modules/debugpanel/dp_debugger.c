/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/syscalls.h>

#include <tilck/kernel/tty.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/tty_struct.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/system_mmap.h>

#include "termutil.h"
#include "dp_int.h"

struct debugger_cmd {

   const char *name;
   const char *alias;
   int  (*handler)(int argc, char **argv);
   const char *help;
};

static int cmd_resolve(int argc, char **argv)
{
   ulong addr = find_addr_of_symbol(argv[0]);

   if (addr) {
      dp_write_raw("%p\n", TO_PTR(addr));
   } else {
      dp_write_raw("Symbol '%s' not found\n", argv[0]);
   }

   return 0;
}

static int cmd_reboot(int argc, char **argv)
{
   reboot();
   printk("Reboot failed. Leaving the system fully halted.");
   return -1;
}

static int cmd_quit(int argc, char **argv)
{
   dp_write_raw("Debugger exit: leaving the system fully halted.");
   return -1;
}

static int cmd_mmap(int argc, char **argv)
{
   dump_memory_map();
   return 0;
}

static void cmd_print_help(void)
{
   dp_write_raw("\n");
   dp_write_raw("Syntax:\n");
   dp_write_raw("    print [<n>] <fmt> <vaddr/sym>\n");
   dp_write_raw("\n");
   dp_write_raw("    <n> is the number of consecutive items to print\n");
   dp_write_raw("    <fmt> is {i|u|x}<bits>\n");
   dp_write_raw("       i - signed integer\n");
   dp_write_raw("       u - unsigned integer\n");
   dp_write_raw("       x - unsigned integer (hex)\n");
   dp_write_raw("\n");
}


static void do_print_val(char type, u64 val, ulong w)
{
   switch (type) {

      case 'x':
         switch (w) {
            case 1:
               dp_write_raw("%02x ", *(u8 *)&val);
               break;
            case 2:
               dp_write_raw("%04x ", *(u16 *)&val);
               break;
            case 4:
               dp_write_raw("%08x ", *(u32 *)&val);
               break;
            case 8:
               dp_write_raw("%16lx ", *(u64 *)&val);
               break;
         }
         break;

      case 'd':
      case 'i':
         switch (w) {
            case 1:
               dp_write_raw("%10d ", *(s8 *)&val);
               break;
            case 2:
               dp_write_raw("%10d ", *(s16 *)&val);
               break;
            case 4:
               dp_write_raw("%10d ", *(s32 *)&val);
               break;
            case 8:
               dp_write_raw("%20ld ", *(s64 *)&val);
               break;
         }
         break;

      case 'u':
         switch (w) {
            case 1:
               dp_write_raw("%10u ", *(u8 *)&val);
               break;
            case 2:
               dp_write_raw("%10u ", *(u16 *)&val);
               break;
            case 4:
               dp_write_raw("%10u ", *(u32 *)&val);
               break;
            case 8:
               dp_write_raw("%20lu ", *(u64 *)&val);
               break;
         }
         break;
   }
}

static int cmd_print(int argc, char **argv)
{
   int rc, error = 0;
   char type;
   ulong vaddr, w, tmp, n = 1;
   u64 val;

   if (argc < 2 || argc > 3) {
      cmd_print_help();
      return 0;
   }

   tmp = tilck_strtoul(argv[0], NULL, 10, &error);

   if (!error) {
      n = tmp;
      argc--; argv++;
   }

   if (argc < 2) {
      cmd_print_help();
      return 0;
   }

   type = argv[0][0];

   if (argv[0][1]) {

      w = tilck_strtoul(argv[0]+1, NULL, 10, &error);

      if (error || (w != 8 && w != 16 && w != 32 && w != 64)) {
         dp_write_raw("Invalid fmt: '%s'\n", argv[0]);
         return 0;
      }

      w >>= 3;

   } else {

      switch (type) {

         case 'x':
            w = 1;
            break;

         default:
            w = 4;
            break;
      }
   }

   if (argv[1][0] == '0' && argv[1][1] == 'x') {

      vaddr = tilck_strtoul(argv[1] + 2, NULL, 16, &error);

      if (error) {
         dp_write_raw("Invalid vaddr: %s\n", argv[1]);
         return 0;
      }

   } else {

      vaddr = find_addr_of_symbol(argv[1]);

      if (!vaddr) {
         dp_write_raw("Symbol '%s' not found\n", argv[1]);
         return 0;
      }
   }

   for (ulong i = 0; i < n; i++) {

      if (!(i % (8/w))) {

         if (i > 0)
            dp_write_raw("\n");

         dp_write_raw("%p: ", vaddr);
      }

      rc = virtual_read(get_curr_pdir(), (void *)vaddr, &val, w);

      if (rc != (int)w) {
         dp_write_raw("Read error at vaddr: %p\n", vaddr);
         return 0;
      }

      do_print_val(type, val, w);
      vaddr += w;
   }

    dp_write_raw("\n");
   return 0;
}

static int cmd_help(int argc, char **argv);

static int cmd_va2pa(int argc, char **argv)
{
   ulong vaddr, pa;
   int err;

   if (argc != 1) {
      dp_write_raw("Expected vaddr\n");
      return 0;
   }

    if (!(argv[0][0] == '0' && argv[0][1] == 'x')) {
      dp_write_raw("Expected vaddr\n");
      return 0;
    }

   vaddr = tilck_strtoul(argv[0] + 2, NULL, 16, &err);

   if (err) {
      dp_write_raw("Expected vaddr\n");
      return 0;
   }

   err = get_mapping2(get_curr_pdir(), TO_PTR(vaddr), &pa);

   if (err) {
      dp_write_raw("The vaddr %p is not mapped in the current pdir\n",
                   TO_PTR(vaddr));
      return 0;
   }

   dp_write_raw("%p\n", TO_PTR(pa));
   return 0;
}

static const struct debugger_cmd all_debug_cmds[] =
{
   { "help", "?", &cmd_help, "show this help" },
   { "resolve", "res", &cmd_resolve, "resolve a symbol to a vaddr" },
   { "print", "p", &cmd_print, "print <n> <fmt> <vaddr/sym>: reads from mem" },
   { "va2pa", NULL, &cmd_va2pa, "va2pa <vaddr>: convert a vaddr to paddr" },
   { "mmap", NULL, &cmd_mmap, "Dump system's memory map" },
   { "quit", NULL, &cmd_quit, "quit the debugger and halt the machine" },
   { "reboot", NULL, &cmd_reboot, "reboot the machine" },
};

static int cmd_help(int argc, char **argv)
{
   dp_write_raw("\n");
   dp_write_raw("%-12s %-8s %s\n", "Name", "Alias", "Help");
   dp_write_raw("----------------------------------------------------------\n");

   for (u32 i = 0; i < ARRAY_SIZE(all_debug_cmds); i++) {

      const struct debugger_cmd *cmd = &all_debug_cmds[i];

      dp_write_raw("%-12s %-8s %s\n",
                   cmd->name, cmd->alias ? cmd->alias : "", cmd->help);
   }

   dp_write_raw("\n");
   return 0;
}

int
dp_mini_debugger_tool()
{
   struct tty *t = get_curr_process_tty();
   fs_handle h;
   char buf[128];
   char *args[8];
   ssize_t rc;
   int last_arg, n_args;

   if (!t) {
      printk("ERROR: the current process has no attached TTY\n");
      return -1;
   }

   disable_preemption();
   {
      devfs_kernel_create_handle_for(t->devfile, &h);
   }
   enable_preemption();

   if (!h) {
      printk("ERROR: cannot open process's TTY\n");
      return -1;
   }

   dp_write_raw("--- Tilck's panic debugger ---\n");

   /* drain the input buffer */
   handle_set_blocking(h, false);

   do {
      rc = vfs_read(h, buf, sizeof(buf));
   } while (rc == -EINTR || rc == sizeof(buf));

   handle_set_blocking(h, true);

   while (true) {

      dp_write_raw("> ");

      bzero(buf, sizeof(buf));
      rc = vfs_read(h, buf, sizeof(buf));

      if (rc < 0)
         break;

      buf[rc] = 0;
      n_args = 0;
      last_arg = 0;

      for (int i = 0; i < rc; i++) {
         if (isspace(buf[i])) {

            if (i != last_arg && !isspace(buf[last_arg]))
               args[n_args++] = buf + last_arg;

            buf[i] = 0;
            last_arg = i + 1;
         }
      }

      if (!n_args)
         continue;

      // for (int i = 0; i < n_args; i++)
      //    dp_write_raw("arg[%d]: '%s'\n", i, args[i]);

      rc = 1;

      for (u32 i = 0; i < ARRAY_SIZE(all_debug_cmds); i++) {

         const struct debugger_cmd *cmd = &all_debug_cmds[i];

         if (!strcmp(cmd->name, args[0]) ||
             (cmd->alias && !strcmp(cmd->alias, args[0])))
         {
            rc = cmd->handler(n_args - 1, args + 1);
            break;
         }
      }

      if (rc < 0)
         break;

      if (rc == 0)
         continue;

      if (n_args > 0)
         dp_write_raw("Unrecognized command '%s'\n", args[0]);
   }

   devfs_kernel_destory_handle(h);
   return rc;
}
