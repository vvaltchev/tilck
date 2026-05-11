/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Task-table data + plain-text dump shared by dp's Tasks panel and
 * the standalone `tracer` binary. See task_dump.h for the rationale.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <tilck/common/syscalls.h>
#include <tilck/common/dp_abi.h>

#include "term.h"
#include "tui_layout.h"
#include "task_dump.h"

struct dp_task_info dp_tasks_buf[MAX_DP_TASKS];
int dp_tasks_count;

static long dp_cmd_get_tasks(struct dp_task_info *buf, unsigned long max)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_GET_TASKS,
                  (long)buf, (long)max, 0L, 0L);
}

int dp_tasks_refresh(void)
{
   long n = dp_cmd_get_tasks(dp_tasks_buf, MAX_DP_TASKS);

   if (n < 0)
      return -1;

   dp_tasks_count = (int)n;
   return 0;
}

void state_to_str(char *out, unsigned char state, bool stopped, bool traced)
{
   char *p = out;

   switch (state) {
      case TS_INVALID:   *p++ = '?'; break;
      case TS_RUNNABLE:  *p++ = 'r'; break;
      case TS_RUNNING:   *p++ = 'R'; break;
      case TS_SLEEPING:  *p++ = 's'; break;
      case TS_ZOMBIE:    *p++ = 'Z'; break;
      default:           *p++ = '?'; break;
   }

   if (stopped) *p++ = 'S';
   if (traced)  *p++ = 't';
   *p = 0;
}

const char *task_dump_str(enum task_dump_str_t t)
{
   static bool initialized;
   static char fmt[120];
   static char hfmt[120];
   static char header[120];
   static char hline_sep[120] = "qqqqqqqnqqqqqqnqqqqqqnqqqqqqnqqqqqnqqqqqn";

   if (!initialized) {

      const int path_field_len = (DP_W - 80) + MAX_EXEC_PATH_LEN;

      snprintf(fmt, sizeof(fmt),
               " %%-5d "
               TERM_VLINE " %%-4d "
               TERM_VLINE " %%-4d "
               TERM_VLINE " %%-4d "
               TERM_VLINE " %%-3s "
               TERM_VLINE "  %%-2d "
               TERM_VLINE " %%-%ds",
               path_field_len);

      snprintf(hfmt, sizeof(hfmt),
               " %%-5s "
               TERM_VLINE " %%-4s "
               TERM_VLINE " %%-4s "
               TERM_VLINE " %%-4s "
               TERM_VLINE " %%-3s "
               TERM_VLINE " %%-3s "
               TERM_VLINE " %%-%ds",
               path_field_len);

      snprintf(header, sizeof(header), hfmt,
               "pid", "pgid", "sid", "ppid", "S", "tty", "cmdline");

      char *p = hline_sep + strlen(hline_sep);
      char *end = hline_sep + sizeof(hline_sep);

      for (int i = 0; i < path_field_len + 2 && p < end; i++, p++)
         *p = 'q';

      initialized = true;
   }

   switch (t) {
      case TDS_HEADER:  return header;
      case TDS_ROW_FMT: return fmt;
      case TDS_HLINE:   return hline_sep;
   }

   return "";
}

/* Plain-text render of one task row. No row counter, no selection
 * highlight — those are panel-only concerns and stay in
 * screen_tasks.c. */
static void
render_task_row_plain(const struct dp_task_info *t, bool kernel_tasks)
{
   const char *fmt = task_dump_str(TDS_ROW_FMT);
   char path[80] = {0};
   char path2[64] = {0};
   char state_str[4];

   if (t->is_kthread && !kernel_tasks)
      return;

   if (t->is_kthread) {
      snprintf(path, sizeof(path), "%s", t->name);
   } else {
      const char *src = t->name[0] ? t->name : "<n/a>";

      if ((int)strlen(src) < MAX_EXEC_PATH_LEN - 2) {
         snprintf(path, sizeof(path), "%s", src);
      } else {
         snprintf(path2, MAX_EXEC_PATH_LEN + 1 - 6, "%s", src);
         snprintf(path, sizeof(path), "%s...", path2);
      }
   }

   state_to_str(state_str, t->state, t->stopped, t->traced);

   /* Master rendered tty=0 for kernel threads (kthreads have no
    * controlling tty). Mirror that here. */
   const int ttynum = t->is_kthread ? 0 : t->tty;

   dp_write_raw(fmt, t->tid, t->pgid, t->sid, t->parent_pid,
                state_str, ttynum, path);
   dp_write_raw("\r\n");
}

/*
 * Public plain-text task dump. Used by the tracer ('p' / 'P' keys)
 * and by dp's `ps` invocation. Refreshes the cached task table first
 * — callers want the latest state, not whatever was in dp_tasks_buf
 * from an earlier panel render.
 */
void dp_dump_task_list_plain(bool kernel_tasks)
{
   if (dp_tasks_refresh() < 0)
      return;

   dp_write_raw("\r\n%s\r\n", task_dump_str(TDS_HEADER));
   dp_write_raw(GFX_ON "%s" GFX_OFF "\r\n", task_dump_str(TDS_HLINE));

   for (int i = 0; i < dp_tasks_count; i++)
      render_task_row_plain(&dp_tasks_buf[i], kernel_tasks);
}
