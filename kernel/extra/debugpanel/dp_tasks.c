/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/cmdline.h>

#include "termutil.h"

static int debug_get_tn_for_tasklet_runner(task_info *ti)
{
   for (u32 i = 0; i < MAX_TASKLET_THREADS; i++)
      if (get_tasklet_runner(i) == ti)
         return (int)i;

   return -1;
}

enum task_dump_util_str {

   HEADER,
   ROW_FMT,
   HLINE
};

static const char *
debug_get_task_dump_util_str(enum task_dump_util_str t)
{
   static bool initialized;
   static char fmt[80] = NO_PREFIX DP_COLOR;
   static char hfmt[80];
   static char header[256];
   static char hline_sep[256] =
      "+-----------+-------+-------+----------+-----+";

   static char *hline_sep_end = &hline_sep[sizeof(hline_sep)];

   if (!initialized) {

      int path_field_len = (DP_W - 80) + 27;

      snprintk(fmt+9, sizeof(fmt)-9,
               "\033[%dC| %%-9d | %%-5d | %%-5d | %%-8s | %%-3d | %%-%ds |\n",
               dp_start_col+1, path_field_len);

      snprintk(hfmt, sizeof(hfmt),
               "| %%-9s | %%-5s | %%-5s | %%-8s | %%-3s | %%-%ds |",
               path_field_len);

      snprintk(header, sizeof(header), hfmt,
               "tid", "pid", "ppid", "state", "tty", "path or kernel thread");

      char *p = hline_sep + strlen(hline_sep);

      for (int i = 0; i < path_field_len + 2 && p < hline_sep_end; i++, p++) {
         *p = '-';
      }

      if (p < hline_sep_end)
         *p++ = '+';

      // if (p < hline_sep_end)
      //    *p++ = '\n';

      initialized = true;
   }

   switch (t) {
      case HEADER:
         return header;

      case ROW_FMT:
         return fmt;

      case HLINE:
         return hline_sep;

      default:
         NOT_REACHED();
   }
}

static int debug_per_task_cb(void *obj, void *arg)
{
   const char *fmt = debug_get_task_dump_util_str(ROW_FMT);
   task_info *ti = obj;

   if (!ti->tid)
      return 0; /* skip the main kernel task */

   const char *state = debug_get_state_name(ti->state);
   int ttynum = tty_get_num(ti->pi->proc_tty);

   if (!is_kernel_thread(ti)) {
      printk(fmt, ti->tid, ti->pi->pid,
             ti->pi->parent_pid, state, ttynum, ti->pi->filepath);
      return 0;
   }

   char buf[128];
   const char *kfunc = find_sym_at_addr((uptr)ti->what, NULL, NULL);

   if (!is_tasklet_runner(ti)) {
      snprintk(buf, sizeof(buf), "<%s>", kfunc);
   } else {
      snprintk(buf, sizeof(buf), "<%s[%d]>",
               kfunc, debug_get_tn_for_tasklet_runner(ti));
   }

   printk(fmt, ti->tid, ti->pi->pid, ti->pi->parent_pid, state, 0, buf);
   return 0;
}

static void debug_dump_task_table_hr(void)
{
   dp_printkln("%s", debug_get_task_dump_util_str(HLINE));
}

void do_show_tasks(void)
{
   debug_dump_task_table_hr();
   dp_printkln("%s", debug_get_task_dump_util_str(HEADER));

   debug_dump_task_table_hr();

   disable_preemption();
   {
      iterate_over_tasks(debug_per_task_cb, NULL);
   }
   enable_preemption();

   debug_dump_task_table_hr();
}
