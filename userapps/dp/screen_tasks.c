/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Tasks panel. Pulls the task table from the kernel via
 * TILCK_CMD_DP_GET_TASKS (struct dp_task_info, see
 * <tilck/common/dp_abi.h>) and renders it the same way the in-kernel
 * dp_tasks.c did. The buffer + the column-format helpers + the
 * plain-text dump live in task_dump.[ch] (shared with the standalone
 * `tracer` binary); this file owns the panel-mode UI — selection,
 * scroll geometry, action keys.
 *
 * Selection mode (ENTER toggles): k/s/c send SIGKILL/SIGSTOP/SIGCONT
 * via the regular kill(2) syscall, t toggles per-task tracing via
 * TILCK_CMD_DP_TASK_SET_TRACED. UP/DOWN move the cursor. ESC exits
 * selection mode. Ctrl+T forks /usr/bin/tracer.
 *
 * ps mode (run via /usr/bin/ps): the same render but plain-text via
 * dp_dump_task_list_plain (task_dump.c), no border, no UI loop.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include <tilck/common/syscalls.h>
#include <tilck/common/dp_abi.h>

#include "term.h"
#include "tui_input.h"
#include "tui_layout.h"
#include "task_dump.h"
#include "dp_int.h"
#include "dp_panel.h"

/*
 * File-scope row counter used by the dp_writeln() macro defined in
 * dp_panel.h. Reset at the top of dp_show_tasks(); incremented by
 * every dp_writeln call (and via dump_task_list / show_actions_menu /
 * render_one_task on the rendering path).
 */
static int row;

/*
 * Panel-local row of the first task in the table. Captured during the
 * render pass so sel_keypress can translate sel_index into the buffer
 * relrow of the highlighted line and decide whether a row_off bump is
 * needed to keep it visible.
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

static long dp_cmd_set_traced(int tid, int enabled)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TASK_SET_TRACED,
                  (long)tid, (long)enabled, 0L, 0L);
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

/* ----------------------- panel-mode rendering ------------------------ */

static void
render_one_task(const struct dp_task_info *t, bool kernel_tasks)
{
   const char *fmt = task_dump_str(TDS_ROW_FMT);
   char path[80] = {0};
   char state_str[4];

   if (t->is_kthread && !kernel_tasks)
      return;

   /* Build the path/cmdline display for the last column */
   if (t->is_kthread) {
      snprintf(path, sizeof(path), "%s", t->name);
   } else {
      const char *src = t->name[0] ? t->name : "<n/a>";

      if ((int)strlen(src) < MAX_EXEC_PATH_LEN - 2) {
         snprintf(path, sizeof(path), "%s", src);
      } else {
         /*
          * Truncate to "<first N chars>..." so the row stays inside
          * the column. %.*s caps the copy length directly — gcc's
          * -Wformat-truncation gets confused by the previous two-
          * snprintf scheme (it sees a 31-byte t->name flowing into a
          * 29-byte intermediate buffer and warns even though the
          * outer if rules that path out).
          */
         snprintf(path, sizeof(path), "%.*s...",
                  MAX_EXEC_PATH_LEN - 6, src);
      }
   }

   state_to_str(state_str, t->state, t->stopped, t->traced);

   /* Master rendered tty=0 for kernel threads (kthreads have no
    * controlling tty). Mirror that here. */
   const int ttynum = t->is_kthread ? 0 : t->tty;

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

static void dump_task_list_panel(bool kernel_tasks)
{
   dp_writeln("%s", task_dump_str(TDS_HEADER));
   dp_writeln(GFX_ON "%s" GFX_OFF, task_dump_str(TDS_HLINE));

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

   /*
    * About to render the first task at this row. Capture the
    * panel-local relrow so sel_keypress can later translate
    * sel_index → buffer position and scroll-on-edge.
    */
   first_task_relrow = row - tui_screen_start_row;

   /*
    * Pin the action menu + table header + hr separator (everything
    * above the first task row) to the top of the panel: those rows
    * shouldn't slide out of view when the user scrolls through a
    * long task list.
    */
   dp_ctx->static_rows = first_task_relrow;

   for (int i = 0; i < dp_tasks_count; i++)
      render_one_task(&dp_tasks_buf[i], kernel_tasks);

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
   row = tui_screen_start_row;

   show_actions_menu();
   dump_task_list_panel(true);
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

   if (sel_relrow - dp_ctx->row_off > tui_screen_rows - 1)
      dp_ctx->row_off = sel_relrow - tui_screen_rows + 1;

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

/* ---------------------- tracer subprocess launch -------------------- *
 *
 * Ctrl+T from this panel used to run the tracer in-process. The
 * tracer is its own binary now, so we fork+execve /usr/bin/tracer,
 * wait for it to exit, and tell the panel main loop to repaint
 * from scratch — the tracer's tui_term_restore swung the terminal
 * back to the default buffer + canonical mode, which we need to
 * undo before drawing the panel again.
 *
 * The per-task .traced flags + the syscall filter live entirely
 * in the kernel, so there's nothing to pass through argv.
 */
static void dp_run_tracer_subprocess(void)
{
   pid_t pid = fork();

   if (pid < 0)
      return;

   if (pid == 0) {

      char *const argv[] = { (char *)"tracer", NULL };
      char *const envp[] = { (char *)"TILCK=1", NULL };
      execve("/initrd/usr/bin/tracer", argv, envp);

      /* exec failed — exit so the parent's waitpid completes. */
      _exit(127);
   }

   int status;
   while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
      continue;

   /* Re-take the terminal: tracer's tui_term_restore reset alt
    * buffer + termios + cursor visibility. The panel's main loop
    * does not re-run tui_term_setup on its own. */
   tui_term_setup();
   dp_force_full_redraw();
}

static enum dp_kb_handler_action
sel_keypress(struct key_event ke)
{
   if (!ke.print_char) {

      if (!strcmp(ke.seq, TUI_KEY_UP)) {
         sel_step(-1);
         ui_need_update = true;
         return dp_kb_handler_ok_and_continue;
      }

      if (!strcmp(ke.seq, TUI_KEY_DOWN)) {
         sel_step(+1);
         ui_need_update = true;
         return dp_kb_handler_ok_and_continue;
      }

      return dp_kb_handler_nak;
   }

   switch (ke.print_char) {

      case TUI_KEY_ESC:
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

      case TUI_KEY_CTRL_T:
         dp_run_tracer_subprocess();
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

   if (ke.print_char == TUI_KEY_ENTER) {
      mode = tm_sel;
      sel_scroll_into_view();   /* in case user PAGE-scrolled past tasks */
      ui_need_update = true;
      return dp_kb_handler_ok_and_continue;
   }

   if (ke.print_char == TUI_KEY_CTRL_T) {
      dp_run_tracer_subprocess();
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
   dp_dump_task_list_plain(false);
   write(STDOUT_FILENO, "\r\n", 2);
   return 0;
}
