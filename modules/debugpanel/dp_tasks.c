/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_modules.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/datetime.h>

#include <tilck/mods/tracing.h>

#include "termutil.h"
#define MAX_EXEC_PATH_LEN     34

void init_dp_tracing(void);
enum kb_handler_action dp_tracing_screen(void);

/* Gfx state */
static int row;

/* State */
static int sel_index;
static int curr_idx;
static int max_idx;
static int sel_tid;
static bool sel_tid_found;

static enum {

   dp_tasks_mode_default,
   dp_tasks_mode_sel,

} mode;

static void
debug_get_state_name(char *s, enum task_state state, bool stopped, bool traced)
{
   char *ptr = s;

   switch (state) {

      case TASK_STATE_INVALID:
         *ptr++ = '?';
         break;

      case TASK_STATE_RUNNABLE:
         *ptr++ = 'r';
         break;

      case TASK_STATE_RUNNING:
         *ptr++ = 'R';
         break;

      case TASK_STATE_SLEEPING:
         *ptr++ = 's';
         break;

      case TASK_STATE_ZOMBIE:
         *ptr++ = 'Z';
         break;

      default:
         NOT_REACHED();
   }

   if (stopped)
      *ptr++ = 'S';

   if (traced)
      *ptr++ = 't';

   *ptr = 0;
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
   static char hline_sep[120] = "qqqqqqqnqqqqqqnqqqqqqnqqqqqqnqqqqqnqqqqqn";

   static char *hline_sep_end = &hline_sep[sizeof(hline_sep)];

   if (!initialized) {

      int path_field_len = (DP_W - 80) + MAX_EXEC_PATH_LEN;

      snprintk(fmt, sizeof(fmt),
               " %%-5d "
               TERM_VLINE " %%-4d "
               TERM_VLINE " %%-4d "
               TERM_VLINE " %%-4d "
               TERM_VLINE " %%-3s "
               TERM_VLINE "  %%-2d "
               TERM_VLINE " %%-%ds",
               dp_start_col+1, path_field_len);

      snprintk(hfmt, sizeof(hfmt),
               " %%-5s "
               TERM_VLINE " %%-4s "
               TERM_VLINE " %%-4s "
               TERM_VLINE " %%-4s "
               TERM_VLINE " %%-3s "
               TERM_VLINE " %%-3s "
               TERM_VLINE " %%-%ds",
               path_field_len);

      snprintk(header,
               sizeof(header),
               hfmt,
               "pid",
               "pgid",
               "sid",
               "ppid",
               "S",
               "tty",
               "cmdline");

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
   char buf[128] = {0};
   char state_str[4];
   char *path = buf;
   char *path2 = buf + MAX_EXEC_PATH_LEN + 1;
   const char *orig_path = pi->debug_cmdline ? pi->debug_cmdline : "<n/a>";

   STATIC_ASSERT(sizeof(buf) >= (2 * MAX_EXEC_PATH_LEN + 2));

   if (ti->tid == KERNEL_TID_START)
      return 0; /* skip the main kernel task */

   if (strlen(orig_path) < MAX_EXEC_PATH_LEN - 2) {
      snprintk(path, MAX_EXEC_PATH_LEN + 1, "%s", orig_path);
   } else {
      snprintk(path2, MAX_EXEC_PATH_LEN + 1 - 6, "%s", orig_path);
      snprintk(path, MAX_EXEC_PATH_LEN + 1, "%s...", path2);
   }

   debug_get_state_name(state_str, ti->state, ti->stopped, ti->traced);
   int ttynum = tty_get_num(ti->pi->proc_tty);

   if (is_kernel_thread(ti)) {

      ttynum = 0;
      const char *kfunc = find_sym_at_addr((uptr)ti->what, NULL, NULL);

      if (kfunc) {
         if (!is_tasklet_runner(ti)) {
            snprintk(buf, sizeof(buf), "<%s>", kfunc);
         } else {
            snprintk(buf, sizeof(buf), "<%s[%d]>",
                     kfunc, debug_get_tn_for_tasklet_runner(ti));
         }
      } else {
         snprintk(buf, sizeof(buf), "<func: %p>", ti->what);
      }
   }

   bool sel = false;

   if (mode == dp_tasks_mode_sel) {

      if (sel_tid > 0) {

         if (ti->tid == sel_tid) {
            sel_index = curr_idx;
            dp_reverse_colors();
            sel = true;
         }

      } else if (sel_index >= 0) {

         if (curr_idx == sel_index) {
            sel_tid = ti->tid;
            dp_reverse_colors();
            sel = true;
         }
      }
   }

   dp_writeln(fmt,
              ti->tid,
              pi->pgid,
              pi->sid,
              pi->parent_pid,
              state_str,
              ttynum,
              buf);

   if (sel)
      dp_reset_attrs();

   curr_idx++;
   return 0;
}

static void debug_dump_task_table_hr(void)
{
   dp_writeln(GFX_ON "%s" GFX_OFF, debug_get_task_dump_util_str(HLINE));
}

static bool is_tid_off_limits(int tid)
{
   if (tid == get_curr_tid())
      return true;

   if (tid >= KERNEL_TID_START)
      return true;

   return false;
}

static enum kb_handler_action
dp_no_tracing_module_action(void)
{
   modal_msg = "The tracing module is NOT built-in";
   return kb_handler_ok_and_continue;
}

static enum kb_handler_action
dp_enter_tracing_mode(void)
{
   return MOD_tracing
      ? dp_tracing_screen()
      : dp_no_tracing_module_action();
}

static enum kb_handler_action
dp_tasks_handle_sel_mode_keypress_special(u32 key)
{
   if (key == KEY_UP) {
      sel_tid = -1;
      sel_index = MAX(0, sel_index - 1);
      ui_need_update = true;
      return kb_handler_ok_and_continue;
   }

   if (key == KEY_DOWN) {
      sel_tid = -1;
      sel_index = MIN(max_idx, sel_index + 1);
      ui_need_update = true;
      return kb_handler_ok_and_continue;
   }

   return kb_handler_nak;
}

static enum kb_handler_action
dp_tasks_handle_sel_mode_keypress_k(void)
{
   if (is_tid_off_limits(sel_tid) || sel_tid == 1) {

      if (sel_tid != get_curr_tid())
         modal_msg = "Killing kernel threads or pid 1 is not allowed";
      else
         modal_msg = "Killing the debug panel's process is not allowed";

      return kb_handler_ok_and_continue;
   }

   ui_need_update = true;
   send_signal(sel_tid, SIGKILL, false);
   return kb_handler_ok_and_continue;
}

static enum kb_handler_action
dp_tasks_handle_sel_mode_keypress_s(void)
{
   if (is_tid_off_limits(sel_tid)) {

      if (sel_tid != get_curr_tid())
         modal_msg = "Stopping kernel threads is not allowed";
      else
         modal_msg = "Stopping the debug panel's process is not allowed";

      return kb_handler_ok_and_continue;
   }

   ui_need_update = true;
   send_signal(sel_tid, SIGSTOP, false);
   return kb_handler_ok_and_continue;
}

static enum kb_handler_action
dp_tasks_handle_sel_mode_keypress_c(void)
{
   if (is_tid_off_limits(sel_tid))
      return kb_handler_ok_and_continue;

   ui_need_update = true;
   send_signal(sel_tid, SIGCONT, false);
   return kb_handler_ok_and_continue;
}

static enum kb_handler_action
dp_tasks_handle_sel_mode_keypress_t(void)
{
   if (is_tid_off_limits(sel_tid)) {

      if (sel_tid != get_curr_tid())
         modal_msg = "Cannot trace kernel threads for syscalls";
      else
         modal_msg = "Cannot trace the debug panel process";

      return kb_handler_ok_and_continue;
   }

   ui_need_update = true;

   {
      disable_preemption();
      struct task *ti = get_task(sel_tid);

      if (ti)
         ti->traced = !ti->traced;

      enable_preemption();
   }

   return kb_handler_ok_and_continue;
}

static enum kb_handler_action
dp_tasks_handle_sel_mode_keypress_esc(void)
{
   mode = dp_tasks_mode_default;
   ui_need_update = true;
   return kb_handler_ok_and_continue;
}

static enum kb_handler_action
dp_tasks_handle_sel_mode_keypress_r(void)
{
   ui_need_update = true;
   return kb_handler_ok_and_continue;
}

static enum kb_handler_action
dp_tasks_handle_sel_mode_keypress(struct key_event ke)
{
   if (!ke.print_char)
      return dp_tasks_handle_sel_mode_keypress_special(ke.key);

   switch (ke.print_char) {

      case DP_KEY_ESC:
         return dp_tasks_handle_sel_mode_keypress_esc();

      case DP_KEY_CTRL_T:
         return dp_enter_tracing_mode();

      case 'r':
         return dp_tasks_handle_sel_mode_keypress_r();

      case 'k':
         return dp_tasks_handle_sel_mode_keypress_k();

      case 's':
         return dp_tasks_handle_sel_mode_keypress_s();

      case 'c':
         return dp_tasks_handle_sel_mode_keypress_c();

      case 't':

         if (MOD_tracing)
            return dp_tasks_handle_sel_mode_keypress_t();

         return dp_no_tracing_module_action();
   }

   return kb_handler_nak;
}

static enum kb_handler_action
dp_tasks_handle_default_mode_enter(void)
{
   mode = dp_tasks_mode_sel;
   ui_need_update = true;
   return kb_handler_ok_and_continue;
}

static enum kb_handler_action
dp_tasks_handle_default_mode_keypress(struct key_event ke)
{
   switch (ke.print_char) {

      case 'r':
         return dp_tasks_handle_sel_mode_keypress_r();

      case DP_KEY_ENTER:
         return dp_tasks_handle_default_mode_enter();

      case DP_KEY_CTRL_T:
         return dp_enter_tracing_mode();
   }

   return kb_handler_nak;
}

static enum kb_handler_action
dp_tasks_keypress(struct key_event ke)
{
   switch (mode) {

      case dp_tasks_mode_default:
         return dp_tasks_handle_default_mode_keypress(ke);

      case dp_tasks_mode_sel:
         return dp_tasks_handle_sel_mode_keypress(ke);
   }

   return kb_handler_nak;
}

static int dp_count_tasks(void *obj, void *arg)
{
   struct task *ti = obj;

   if (ti->tid == KERNEL_TID_START)
      return 0; /* skip the main kernel task */

   if (mode == dp_tasks_mode_sel) {
      if (sel_tid > 0 && ti->tid == sel_tid)
         sel_tid_found = true;
   }

   max_idx++;
   return 0;
}

static void show_actions_menu(void)
{
   if (mode == dp_tasks_mode_default) {

      dp_writeln(
         E_COLOR_BR_WHITE "<ENTER>" RESET_ATTRS ": select mode " TERM_VLINE " "
         E_COLOR_BR_WHITE "r" RESET_ATTRS ": refresh " TERM_VLINE " "
         E_COLOR_BR_WHITE "Ctrl+T" RESET_ATTRS ": show trace"
      );

      dp_writeln("");

   } else if (mode == dp_tasks_mode_sel) {

      dp_writeln(
         E_COLOR_BR_WHITE "ESC" RESET_ATTRS ": exit select mode " TERM_VLINE " "
         E_COLOR_BR_WHITE "r" RESET_ATTRS ": refresh " TERM_VLINE " "
         E_COLOR_BR_WHITE "Ctrl+T" RESET_ATTRS ": show trace " TERM_VLINE " "
         E_COLOR_BR_WHITE "t" RESET_ATTRS ": trace task "
      );

      dp_writeln(
         E_COLOR_BR_WHITE "k" RESET_ATTRS ": kill " TERM_VLINE " "
         E_COLOR_BR_WHITE "s" RESET_ATTRS ": stop " TERM_VLINE " "
         E_COLOR_BR_WHITE "c" RESET_ATTRS ": continue "
      );

   }

   dp_writeln("");
}

static void dp_show_tasks(void)
{
   row = dp_screen_start_row;

   show_actions_menu();
   dp_writeln("%s", debug_get_task_dump_util_str(HEADER));
   debug_dump_task_table_hr();

   disable_preemption();
   {
      curr_idx = 0;
      max_idx = -1;
      sel_tid_found = false;
      iterate_over_tasks(dp_count_tasks, NULL);

      if (mode == dp_tasks_mode_sel && sel_tid > 0 && !sel_tid_found) {

         /*
          * The task with the selected tid does not exist anymore: invalidate
          * the selected TID, but leave sel_index as it is: this will make
          * the cursor to stay at the same position and select the next task.
          */
         sel_tid = -1;
      }

      if (sel_index >= 0)
         sel_index = MIN(sel_index, max_idx);

      iterate_over_tasks(debug_per_task_cb, NULL);
   }
   enable_preemption();
   dp_writeln("");
}

static void dp_tasks_enter(void)
{
   sel_index = 0;
   sel_tid = -1;
   mode = dp_tasks_mode_default;
}

static void dp_tasks_exit(void)
{
   /* do nothing, for the moment */
}

static void dp_tasks_first_setup(void)
{
   if (MOD_tracing)
      init_dp_tracing();
}

static struct dp_screen dp_tasks_screen =
{
   .index = 3,
   .label = "Tasks",
   .first_setup = dp_tasks_first_setup,
   .on_dp_enter = dp_tasks_enter,
   .on_dp_exit = dp_tasks_exit,
   .draw_func = dp_show_tasks,
   .on_keypress_func = dp_tasks_keypress,
};

__attribute__((constructor))
static void dp_tasks_init(void)
{
   dp_register_screen(&dp_tasks_screen);
}
