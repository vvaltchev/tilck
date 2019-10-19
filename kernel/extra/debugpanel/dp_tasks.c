/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/cmdline.h>

#include "termutil.h"
#define MAX_EXEC_PATH_LEN     36

static int row;

const char *debug_get_state_name(enum task_state state)
{
   switch (state) {

      case TASK_STATE_INVALID:
         return "?";

      case TASK_STATE_RUNNABLE:
         return "r";

      case TASK_STATE_RUNNING:
         return "R";

      case TASK_STATE_SLEEPING:
         return "s";

      case TASK_STATE_ZOMBIE:
         return "Z";

      default:
         NOT_REACHED();
   }
}

static int debug_get_tn_for_tasklet_runner(struct task *ti)
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
   static char fmt[120];
   static char hfmt[120];
   static char header[120];
   static char hline_sep[120] = "qqqqqqqqqqqnqqqqqqqnqqqqqqqnqqqnqqqqqn";

   static char *hline_sep_end = &hline_sep[sizeof(hline_sep)];

   if (!initialized) {

      int path_field_len = (DP_W - 80) + MAX_EXEC_PATH_LEN;

      snprintk(fmt, sizeof(fmt),
               " %%-9d "
               TERM_VLINE " %%-5d "
               TERM_VLINE " %%-5d "
               TERM_VLINE " %%-1s "
               TERM_VLINE " %%-3d "
               TERM_VLINE " %%-%ds",
               dp_start_col+1, path_field_len);

      snprintk(hfmt, sizeof(hfmt),
               " %%-9s "
               TERM_VLINE " %%-5s "
               TERM_VLINE " %%-5s "
               TERM_VLINE " %%-1s "
               TERM_VLINE " %%-3s "
               TERM_VLINE " %%-%ds",
               path_field_len);

      snprintk(header, sizeof(header), hfmt,
               "tid", "pid", "ppid", "S", "tty", "path or kernel thread");

      char *p = hline_sep + strlen(hline_sep);

      for (int i = 0; i < path_field_len + 2 && p < hline_sep_end; i++, p++) {
         *p = 'q';
      }

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
   struct task *ti = obj;
   struct process *pi = ti->pi;
   char buf[128];
   char path[MAX_EXEC_PATH_LEN + 1];
   char path2[MAX_EXEC_PATH_LEN + 1];
   const char *orig_path = pi->debug_filepath;

   if (!ti->tid)
      return 0; /* skip the main kernel task */

   if (strlen(orig_path) < MAX_EXEC_PATH_LEN - 2) {
      snprintk(path, sizeof(path), "%s", orig_path);
   } else {
      snprintk(path2, sizeof(path) - 6, "%s", orig_path);
      snprintk(path, sizeof(path), "%s...", path2);
   }

   const char *state = debug_get_state_name(ti->state);
   int ttynum = tty_get_num(ti->pi->proc_tty);

   if (!is_kernel_thread(ti)) {
      dp_writeln(fmt, ti->tid, pi->pid,
                 pi->parent_pid, state, ttynum, path);
      return 0;
   }

   const char *kfunc = find_sym_at_addr((uptr)ti->what, NULL, NULL);

   if (!is_tasklet_runner(ti)) {
      snprintk(buf, sizeof(buf), "<%s>", kfunc);
   } else {
      snprintk(buf, sizeof(buf), "<%s[%d]>",
               kfunc, debug_get_tn_for_tasklet_runner(ti));
   }

   dp_writeln(fmt, ti->tid, pi->pid, pi->parent_pid, state, 0, buf);
   return 0;
}

static void debug_dump_task_table_hr(void)
{
   dp_writeln(GFX_ON "%s" GFX_OFF, debug_get_task_dump_util_str(HLINE));
}

static void dp_show_tasks(void)
{
   row = dp_screen_start_row;
   dp_writeln("%s", debug_get_task_dump_util_str(HEADER));
   debug_dump_task_table_hr();

   disable_preemption();
   {
      iterate_over_tasks(debug_per_task_cb, NULL);
   }
   enable_preemption();
   dp_writeln("");
}

static dp_screen dp_tasks_screen =
{
   .index = 3,
   .label = "Tasks",
   .draw_func = dp_show_tasks,
   .on_keypress_func = NULL,
};

__attribute__((constructor))
static void dp_tasks_init(void)
{
   dp_register_screen(&dp_tasks_screen);
}
