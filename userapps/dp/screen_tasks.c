/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Tasks panel. Pulls the task table from the kernel via
 * TILCK_CMD_DP_GET_TASKS (struct dp_task_info, see
 * <tilck/common/dp_abi.h>) and renders it the same way the in-kernel
 * dp_tasks.c did.
 *
 * Selection mode (ENTER toggles): k/s/c send SIGKILL/SIGSTOP/SIGCONT
 * via the regular kill(2) syscall, t toggles per-task tracing via
 * TILCK_CMD_DP_TASK_SET_TRACED. UP/DOWN move the cursor. ESC exits
 * selection mode.
 *
 * ps mode (run via /usr/bin/ps): the same render but plain-text via
 * dp_write_raw, no border, no UI loop.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <tilck/common/syscalls.h>
#include <tilck/common/dp_abi.h>

#include "termutil.h"
#include "dp_int.h"

#define MAX_EXEC_PATH_LEN     34
#define MAX_DP_TASKS         512

/* Task state bytes stored in dp_task_info.state — values from
 * <tilck/kernel/sched.h> (TASK_STATE_*). */
#define TS_INVALID    0
#define TS_RUNNABLE   1
#define TS_RUNNING    2
#define TS_SLEEPING   3
#define TS_ZOMBIE     4

static struct dp_task_info dp_tasks_buf[MAX_DP_TASKS];
static int dp_tasks_count;

/*
 * File-scope row counter used by the dp_writeln() macro defined in
 * termutil.h. Reset at the top of dp_show_tasks(); incremented by
 * every dp_writeln call (and via dump_task_list / show_actions_menu /
 * render_one_task on the rendering path).
 */
static int row;

/*
 * Panel-local row of the first task in the table. Captured during the
 * (panel-mode) render pass so sel_keypress can translate sel_index
 * into the buffer relrow of the highlighted line and decide whether
 * a row_off bump is needed to keep it visible.
 */
static int first_task_relrow;

/* Selection-mode state. */
static enum {
   tm_default,
   tm_sel,
} mode;

static int sel_index;       /* selected row when sel_tid invalid */
static int sel_tid;         /* selected task id, -1 when only sel_index valid */
static int max_idx;
static bool sel_tid_found;

/* ------------------------------ helpers ------------------------------ */

static long dp_cmd_get_tasks(struct dp_task_info *buf, unsigned long max)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_GET_TASKS,
                  (long)buf, (long)max, 0L, 0L);
}

static long dp_cmd_set_traced(int tid, int enabled)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TASK_SET_TRACED,
                  (long)tid, (long)enabled, 0L, 0L);
}

static void
state_to_str(char *out, unsigned char state, bool stopped, bool traced)
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

enum task_dump_str_t {
   TDS_HEADER,
   TDS_ROW_FMT,
   TDS_HLINE,
};

static const char *
task_dump_str(enum task_dump_str_t t)
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

static int dp_tasks_refresh(void)
{
   long n = dp_cmd_get_tasks(dp_tasks_buf, MAX_DP_TASKS);

   if (n < 0)
      return -1;

   dp_tasks_count = (int)n;
   return 0;
}

static bool is_tid_off_limits(int tid)
{
   /*
    * The dp tool's own pid is off-limits (don't kill ourselves).
    * Kernel threads (is_kthread) are off-limits too. We approximate
    * "kernel thread" by checking the is_kthread bit in the cached
    * dp_task_info entry.
    */

   if (tid == (int)getpid())
      return true;

   for (int i = 0; i < dp_tasks_count; i++) {

      if (dp_tasks_buf[i].tid == tid)
         return dp_tasks_buf[i].is_kthread != 0;
   }

   return true;
}

/* ----------------------- rendering / dumping ------------------------- */

struct render_opts {
   bool kernel_tasks;
   bool plain_text;
};

static void
render_one_task(const struct dp_task_info *t, struct render_opts opts)
{
   const char *fmt = task_dump_str(TDS_ROW_FMT);
   char path[80] = {0};
   char path2[64] = {0};
   char state_str[4];

   if (t->is_kthread && !opts.kernel_tasks)
      return;

   /* Build the path/cmdline display for the last column */
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

   if (opts.plain_text) {

      dp_write_raw(fmt, t->tid, t->pgid, t->sid, t->parent_pid,
                   state_str, ttynum, path);
      dp_write_raw("\r\n");
      return;
   }

   /*
    * In selection mode, wrap the row in REVERSE_VIDEO/RESET_ATTRS so
    * the selected line is highlighted. The escapes are baked into the
    * format string rather than emitted around the dp_writeln call,
    * because dp_writeln now appends to a row buffer (no notion of
    * "current attrs" outside of a single call).
    */
   const bool selected = (mode == tm_sel) &&
                         (sel_tid > 0) &&
                         (t->tid == sel_tid);

   if (selected) {

      char rev_fmt[256];
      snprintf(rev_fmt, sizeof(rev_fmt),
               REVERSE_VIDEO "%s" RESET_ATTRS, fmt);
      dp_writeln(rev_fmt, t->tid, t->pgid, t->sid, t->parent_pid,
                 state_str, ttynum, path);

   } else {

      dp_writeln(fmt, t->tid, t->pgid, t->sid, t->parent_pid,
                 state_str, ttynum, path);
   }
}

static void
dump_table_hr(bool plain_text)
{
   const char *hr = task_dump_str(TDS_HLINE);

   if (plain_text)
      dp_write_raw(GFX_ON "%s" GFX_OFF "\r\n", hr);
   else
      dp_writeln(GFX_ON "%s" GFX_OFF, hr);
}

static void
dump_task_list(bool kernel_tasks, bool plain_text)
{
   struct render_opts opts = {
      .kernel_tasks = kernel_tasks,
      .plain_text = plain_text,
   };

   if (plain_text)
      dp_write_raw("\r\n%s\r\n", task_dump_str(TDS_HEADER));
   else
      dp_writeln("%s", task_dump_str(TDS_HEADER));

   dump_table_hr(plain_text);

   /* Validate sel_tid before rendering: it might have died since the
    * last refresh. */
   sel_tid_found = false;
   max_idx = -1;

   for (int i = 0; i < dp_tasks_count; i++) {

      const struct dp_task_info *t = &dp_tasks_buf[i];

      if (t->is_kthread && !kernel_tasks)
         continue;

      if (mode == tm_sel && sel_tid > 0 && t->tid == sel_tid)
         sel_tid_found = true;

      max_idx++;
   }

   if (mode == tm_sel && sel_tid > 0 && !sel_tid_found)
      sel_tid = -1;     /* selected task gone; let sel_index pick the next */

   if (mode == tm_sel && sel_tid <= 0) {

      /* Promote sel_index to a concrete tid for the row about to render. */
      int idx = 0;

      if (sel_index < 0)        sel_index = 0;
      if (sel_index > max_idx)  sel_index = max_idx;

      for (int i = 0; i < dp_tasks_count; i++) {

         if (dp_tasks_buf[i].is_kthread && !kernel_tasks)
            continue;

         if (idx == sel_index) {
            sel_tid = dp_tasks_buf[i].tid;
            break;
         }

         idx++;
      }
   }

   if (!plain_text) {

      /* About to render the first task at this row. Capture the
       * panel-local relrow so sel_keypress can later translate
       * sel_index → buffer position and scroll-on-edge.
       *
       * (In plain-text ps mode there's no scrolling viewport, so we
       * skip the capture; first_task_relrow is unused there.)
       */
      first_task_relrow = row - dp_screen_start_row;

      /*
       * Pin the action menu + table header + hr separator (everything
       * above the first task row) to the top of the panel: those rows
       * shouldn't slide out of view when the user scrolls through a
       * long task list.
       */
      dp_ctx->static_rows = first_task_relrow;
   }

   for (int i = 0; i < dp_tasks_count; i++)
      render_one_task(&dp_tasks_buf[i], opts);

   /*
    * No trailing dp_writeln(" "). The painter naturally clears
    * unused rows below the last task, so a buffered blank line would
    * just inflate row_max (and therefore the "rows X-Y of Z" counter)
    * without any visual effect.
    */
}

/* ---------------------------- panel API ------------------------------ */

static void show_actions_menu(void)
{
   if (mode == tm_default) {

      dp_writeln(
         E_COLOR_BR_WHITE "<ENTER>" RESET_ATTRS ": select mode " TERM_VLINE " "
         E_COLOR_BR_WHITE "r" RESET_ATTRS ": refresh " TERM_VLINE " "
         E_COLOR_BR_WHITE "Ctrl+T" RESET_ATTRS ": tracing mode"
      );
      dp_writeln(" ");

   } else {

      dp_writeln(
         E_COLOR_BR_WHITE "ESC" RESET_ATTRS ": exit select " TERM_VLINE " "
         E_COLOR_BR_WHITE "r" RESET_ATTRS ": refresh " TERM_VLINE " "
         E_COLOR_BR_WHITE "Ctrl+T" RESET_ATTRS ": tracing mode " TERM_VLINE " "
         E_COLOR_BR_WHITE "t" RESET_ATTRS ": trace task"
      );
      dp_writeln(
         E_COLOR_BR_WHITE "k" RESET_ATTRS ": kill " TERM_VLINE " "
         E_COLOR_BR_WHITE "s" RESET_ATTRS ": stop " TERM_VLINE " "
         E_COLOR_BR_WHITE "c" RESET_ATTRS ": continue"
      );
   }

   dp_writeln(" ");
}

static void dp_show_tasks(void)
{
   row = dp_screen_start_row;

   show_actions_menu();
   dump_task_list(true, false);
}

static void dp_tasks_enter(void)
{
   sel_index = 0;
   sel_tid = -1;
   mode = tm_default;
   dp_tasks_refresh();
}

/*
 * If the highlighted task's buffer row falls outside the SCROLLABLE
 * viewport (panel rows [static_rows, screen_rows)), scroll by the
 * minimum amount that brings it back inside. Called whenever
 * sel_index changes (UP/DOWN arrows) or when entering select mode
 * (ENTER).
 *
 * The row_off semantic is the offset within the scrollable region —
 * the static rows above (action menu + table header + hr) stay
 * pinned regardless. So sel is visible iff
 *   sel_relrow - row_off ∈ [static_rows, screen_rows - 1]
 * and we clamp row_off to that interval.
 *
 * Uses the previous render's first_task_relrow / static_rows; that's
 * safe because the table layout in this screen doesn't change between
 * renders.
 */
static void sel_scroll_into_view(void)
{
   const int sel_relrow = first_task_relrow + sel_index;
   const int static_rows = dp_ctx->static_rows;

   if (sel_relrow - dp_ctx->row_off < static_rows)
      dp_ctx->row_off = sel_relrow - static_rows;

   if (sel_relrow - dp_ctx->row_off > dp_screen_rows - 1)
      dp_ctx->row_off = sel_relrow - dp_screen_rows + 1;

   if (dp_ctx->row_off < 0)
      dp_ctx->row_off = 0;
}

/*
 * Cursor-step UP/DOWN. Move sel by one task and let
 * sel_scroll_into_view() pull the viewport along iff sel would now
 * fall outside it. Inside the viewport sel moves freely without
 * disturbing row_off — so e.g. UP from the last-visible row just
 * walks the highlight up through the viewport (no scroll), and only
 * an UP from the first-visible row triggers a one-row scroll.
 */
static void sel_step(int direction)
{
   sel_tid = -1;

   if (direction < 0) {

      if (sel_index > 0)
         sel_index--;

   } else {

      if (sel_index < max_idx)
         sel_index++;
   }

   sel_scroll_into_view();
}

static enum dp_kb_handler_action
sel_keypress(struct key_event ke)
{
   if (!ke.print_char) {

      if (!strcmp(ke.seq, DP_KEY_UP)) {
         sel_step(-1);
         ui_need_update = true;
         return dp_kb_handler_ok_and_continue;
      }

      if (!strcmp(ke.seq, DP_KEY_DOWN)) {
         sel_step(+1);
         ui_need_update = true;
         return dp_kb_handler_ok_and_continue;
      }

      return dp_kb_handler_nak;
   }

   switch (ke.print_char) {

      case DP_KEY_ESC:
         mode = tm_default;
         ui_need_update = true;
         return dp_kb_handler_ok_and_continue;

      case 'r':
         dp_tasks_refresh();
         ui_need_update = true;
         return dp_kb_handler_ok_and_continue;

      case 'k':
         if (sel_tid > 0 && !is_tid_off_limits(sel_tid)) {
            kill(sel_tid, SIGKILL);
            dp_tasks_refresh();
         } else {
            modal_msg = "Killing kernel threads or self is not allowed";
         }
         ui_need_update = true;
         return dp_kb_handler_ok_and_continue;

      case 's':
         if (sel_tid > 0 && !is_tid_off_limits(sel_tid)) {
            kill(sel_tid, SIGSTOP);
            dp_tasks_refresh();
         } else {
            modal_msg = "Stopping kernel threads or self is not allowed";
         }
         ui_need_update = true;
         return dp_kb_handler_ok_and_continue;

      case 'c':
         if (sel_tid > 0 && !is_tid_off_limits(sel_tid)) {
            kill(sel_tid, SIGCONT);
            dp_tasks_refresh();
         }
         ui_need_update = true;
         return dp_kb_handler_ok_and_continue;

      case 't':
         if (sel_tid > 0 && !is_tid_off_limits(sel_tid)) {

            /* Toggle: read the cached state, send the opposite. */
            bool currently_traced = false;

            for (int i = 0; i < dp_tasks_count; i++) {
               if (dp_tasks_buf[i].tid == sel_tid) {
                  currently_traced = dp_tasks_buf[i].traced != 0;
                  break;
               }
            }

            if (dp_cmd_set_traced(sel_tid, !currently_traced) < 0)
               modal_msg = "Tracing toggle failed (module not loaded?)";
            else
               dp_tasks_refresh();
         } else {
            modal_msg = "Cannot trace kernel threads or self";
         }
         ui_need_update = true;
         return dp_kb_handler_ok_and_continue;

      case DP_KEY_CTRL_T:
         dp_run_tracer_screen();
         ui_need_update = true;
         return dp_kb_handler_ok_and_continue;
   }

   return dp_kb_handler_nak;
}

static enum dp_kb_handler_action
default_keypress(struct key_event ke)
{
   if (ke.print_char == 'r') {
      dp_tasks_refresh();
      ui_need_update = true;
      return dp_kb_handler_ok_and_continue;
   }

   if (ke.print_char == DP_KEY_ENTER) {
      mode = tm_sel;
      sel_scroll_into_view();   /* in case user PAGE-scrolled past tasks */
      ui_need_update = true;
      return dp_kb_handler_ok_and_continue;
   }

   if (ke.print_char == DP_KEY_CTRL_T) {
      dp_run_tracer_screen();
      ui_need_update = true;
      return dp_kb_handler_ok_and_continue;
   }

   return dp_kb_handler_nak;
}

static enum dp_kb_handler_action
dp_tasks_keypress(struct key_event ke)
{
   if (mode == tm_sel)
      return sel_keypress(ke);

   return default_keypress(ke);
}

static struct dp_screen dp_tasks_screen = {
   .index = 3,
   .label = "Tasks",
   .draw_func = dp_show_tasks,
   .on_dp_enter = dp_tasks_enter,
   .on_keypress_func = dp_tasks_keypress,
};

__attribute__((constructor))
static void dp_tasks_register(void)
{
   dp_register_screen(&dp_tasks_screen);
}

/* ----------------------- ps mode entry point ------------------------- */

int dp_run_ps(void)
{
   if (dp_tasks_refresh() < 0) {
      fprintf(stderr, "ps: TILCK_CMD_DP_GET_TASKS failed (errno=%d)\n", errno);
      return 1;
   }

   dump_task_list(false, true);
   write(STDOUT_FILENO, "\r\n", 2);
   return 0;
}

/*
 * Public plain-text task dump used by the tracer ('p' / 'P' keys).
 * Refreshes the cached task table first; the tracer wants the latest
 * state, not whatever was in dp_tasks_buf from the last panel render.
 */
void dp_dump_task_list_plain(bool kernel_tasks)
{
   const __typeof__(mode) saved = mode;

   if (dp_tasks_refresh() < 0)
      return;

   /* The selection highlight only makes sense inside the panel
    * render path; while we render plain text for the tracer, force
    * default mode so the dump skips the highlight branch. */
   mode = tm_default;
   dump_task_list(kernel_tasks, true);
   mode = saved;
}
