/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Userspace tracer screen — full-screen mode entered via the
 * `tracer` binary's main(). Ctrl+T in the dp Tasks panel reaches
 * this code via fork+exec of /usr/bin/tracer (see screen_tasks.c).
 *
 * Master implemented this entirely in the kernel (modules/debugpanel/
 * dp_tracing.c + dp_tracing_sys.c). After the userspace move the
 * rendering lives in tr_render.c / tr_dump*.c and is driven by
 * metadata fetched from /syst/tracing/metadata; events are streamed
 * via /syst/tracing/events.
 *
 * Everything else — banner, help, key dispatch, filter prompts,
 * traced-PID list editor, "discard remaining events?" prompt — is
 * pure UI and lives here.
 *
 * Keys (matches master's set):
 *   o : toggle "always ENTER+EXIT" (force_exp_block)
 *   b : toggle "Big bufs" dump
 *   e : edit syscall wildcard expression
 *   k : set trace_printk() level
 *   l : list traced syscalls
 *   p : dump user task list
 *   P : dump full task list (incl. kthreads)
 *   t : edit comma-separated list of traced PIDs
 *   h : help
 *   q : quit (back to dp panel if entered via Ctrl+T)
 *   ENTER : start / stop tracing
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <tilck/common/syscalls.h>
#include <tilck/common/dp_abi.h>

#include "term.h"
#include "tui_input.h"
#include "tui_layout.h"
#include "task_dump.h"
#include "tr.h"

#define EVENTS_PATH       "/syst/tracing/events"
#define RENDER_BUF_SZ     1024
#define MAX_SYSCALLS      512

/* ----------------------- TILCK_CMD wrappers -------------------------- */

static long
dp_cmd_get_stats(struct dp_trace_stats *out)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_GET_STATS,
                  (long)out, 0L, 0L, 0L);
}

static long
dp_cmd_get_filter(char *buf, unsigned long buf_sz)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_GET_FILTER,
                  (long)buf, (long)buf_sz, 0L, 0L);
}

static long
dp_cmd_set_filter(const char *expr)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_SET_FILTER,
                  (long)expr, 0L, 0L, 0L);
}

static long
dp_cmd_set_enabled(int enabled)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_SET_ENABLED,
                  (long)enabled, 0L, 0L, 0L);
}

static long
dp_cmd_set_force_exp_block(int v)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_SET_FORCE_EXP_BLOCK,
                  (long)v, 0L, 0L, 0L);
}

static long
dp_cmd_set_dump_big_bufs(int v)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_SET_DUMP_BIG_BUFS,
                  (long)v, 0L, 0L, 0L);
}

static long
dp_cmd_set_printk_lvl(int lvl)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_SET_PRINTK_LVL,
                  (long)lvl, 0L, 0L, 0L);
}

static long
dp_cmd_get_traced_bitmap(unsigned char *buf, unsigned long buf_sz)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_GET_TRACED_BITMAP,
                  (long)buf, (long)buf_sz, 0L, 0L);
}

static long
dp_cmd_get_in_buf_count(void)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_GET_IN_BUF_COUNT,
                  0L, 0L, 0L, 0L);
}

static long
dp_cmd_get_traced_tids_and_clear(int *buf, unsigned long max)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TASK_GET_TRACED_TIDS_AND_CLEAR,
                  (long)buf, (long)max, 0L, 0L);
}

static long
dp_cmd_set_task_traced(int tid, int enabled)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TASK_SET_TRACED,
                  (long)tid, (long)enabled, 0L, 0L);
}

/*
 * Trace events are now rendered locally by tr_render_event()
 * (userapps/dp/tr_render.c) using metadata read from
 * /syst/tracing/metadata. The kernel-side renderer the wrapper used
 * to call (TILCK_CMD_DP_TRACE_RENDER_EVENT) is being removed in a
 * follow-up cleanup commit; the slot is still defined as a
 * deprecated NULL.
 */

static long
dp_cmd_get_sys_name(unsigned sys_n, char *buf, unsigned long buf_sz)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_GET_SYS_NAME,
                  (long)sys_n, (long)buf, (long)buf_sz, 0L);
}

/* ----------------------------- banner UI ----------------------------- */

static char filter_buf[DP_TRACE_FILTER_MAX];

static void show_banner(void)
{
   struct dp_trace_stats st = {0};
   long rc;

   if (dp_cmd_get_stats(&st) < 0) {
      term_write(E_COLOR_BR_RED
                   "TILCK_CMD_DP_TRACE_GET_STATS failed (errno=%d)\r\n"
                   RESET_ATTRS, errno);
      return;
   }

   /*
    * Mirror the kernel-side toggles into the userspace renderer's
    * local state. The renderer reads these flags when deciding
    * whether to render the ENTER event for non-blocking syscalls
    * (`force_exp_block`) and whether to truncate large buffers
    * (`dump_big_bufs`). Pushing them after every banner refresh
    * keeps the renderer in sync without needing its own polling.
    */
   tr_set_force_exp_block(st.force_exp_block ? true : false);
   tr_set_dump_big_bufs(st.dump_big_bufs ? true : false);

   filter_buf[0] = '\0';
   rc = dp_cmd_get_filter(filter_buf, sizeof(filter_buf));
   if (rc < 0)
      strcpy(filter_buf, "?");

   term_write(E_COLOR_YELLOW
                "Tilck syscall tracing (h: help)\r\n"
                RESET_ATTRS);

   term_write(
      TERM_VLINE " Always ENTER+EXIT: %s "
      TERM_VLINE " Big bufs: %s  "
      TERM_VLINE " #Sys traced: " E_COLOR_BR_BLUE "%d" RESET_ATTRS " "
      TERM_VLINE " #Tasks traced: " E_COLOR_BR_BLUE "%d" RESET_ATTRS " "
      TERM_VLINE "\r\n"
      TERM_VLINE " Printk lvl: " E_COLOR_BR_BLUE "%d" RESET_ATTRS
      "\r\n",
      st.force_exp_block ? E_COLOR_GREEN "ON" RESET_ATTRS
                         : E_COLOR_RED "OFF" RESET_ATTRS,
      st.dump_big_bufs   ? E_COLOR_GREEN "ON" RESET_ATTRS
                         : E_COLOR_RED "OFF" RESET_ATTRS,
      st.sys_traced_count,
      st.tasks_traced_count,
      st.printk_lvl);

   term_write(TERM_VLINE " Trace expr: " E_COLOR_YELLOW "%s" RESET_ATTRS,
                filter_buf);

   term_write("\r\n");
   term_write(E_COLOR_YELLOW "> " RESET_ATTRS);
}

static void show_help(void)
{
   term_write("\r\n\r\n");
   term_write(E_COLOR_YELLOW "Tracing mode help" RESET_ATTRS "\r\n");

   term_write("  " E_COLOR_YELLOW "o" RESET_ATTRS
                "     : Toggle always enter + exit\r\n");
   term_write("  " E_COLOR_YELLOW "b" RESET_ATTRS
                "     : Toggle dump big buffers\r\n");
   term_write("  " E_COLOR_YELLOW "e" RESET_ATTRS
                "     : Edit syscalls wildcard expr "
                E_COLOR_RED "[1]" RESET_ATTRS "\r\n");
   term_write("  " E_COLOR_YELLOW "k" RESET_ATTRS
                "     : Set trace_printk() level\r\n");
   term_write("  " E_COLOR_YELLOW "l" RESET_ATTRS
                "     : List traced syscalls\r\n");
   term_write("  " E_COLOR_YELLOW "p" RESET_ATTRS
                "     : Dump user tasks list\r\n");
   term_write("  " E_COLOR_YELLOW "P" RESET_ATTRS
                "     : Dump full task list\r\n");
   term_write("  " E_COLOR_YELLOW "t" RESET_ATTRS
                "     : Edit list of traced PIDs\r\n");
   term_write("  " E_COLOR_YELLOW "q" RESET_ATTRS
                "     : Back to the debug panel\r\n");
   term_write("  " "ENTER" RESET_ATTRS " : Start / stop tracing\r\n");

   term_write("\r\n" E_COLOR_RED "[1]" RESET_ATTRS " ");
   term_write("In the wildcard expr the " E_COLOR_BR_WHITE "*" RESET_ATTRS
                " character is allowed only once, at the end.\r\n");
   term_write("The " E_COLOR_BR_WHITE "!" RESET_ATTRS " character can be "
                "used, at the beginning of each sub-expr, to negate it.\r\n");
   term_write("Single sub-expressions are separated by comma or space. "
                "The " E_COLOR_BR_WHITE "?" RESET_ATTRS " character is\r\n");
   term_write("supported and has the usual meaning "
                "(matches 1 single char, any).\r\n");
   term_write(E_COLOR_BR_WHITE "Example: " RESET_ATTRS
                "read*,write*,!readlink* \r\n");
}

/* ---------------------- 'l' list traced syscalls --------------------- */

static void list_traced_syscalls(void)
{
   unsigned char bitmap[MAX_SYSCALLS];
   long n;

   term_write("\r\n\r\n");
   term_write(E_COLOR_YELLOW "Traced syscalls list" RESET_ATTRS);
   term_write("\r\n");

   n = dp_cmd_get_traced_bitmap(bitmap, sizeof(bitmap));

   if (n < 0) {
      term_write(E_COLOR_RED "Failed to retrieve traced syscalls "
                   "(errno=%d)\r\n" RESET_ATTRS, errno);
      return;
   }

   for (long i = 0; i < n; i++) {

      if (!bitmap[i])
         continue;

      char name[DP_SYS_NAME_MAX];
      long len = dp_cmd_get_sys_name((unsigned)i, name, sizeof(name));

      if (len < 0)
         continue;

      const char *p = name;

      /* Strip "sys_" prefix to match master. */
      if (!strncmp(p, "sys_", 4))
         p += 4;

      term_write("%s ", p);
   }

   term_write("\r\n");
}

/* ----------------------- 'e' edit filter expr ------------------------ */

static void edit_filter(void)
{
   char buf[DP_TRACE_FILTER_MAX];
   buf[0] = '\0';

   dp_cmd_get_filter(buf, sizeof(buf));

   term_move_left(2);
   term_write(E_COLOR_YELLOW "expr> " RESET_ATTRS);
   tui_set_input_blocking(true);
   tui_read_line(buf, sizeof(buf));
   tui_set_input_blocking(false);

   if (dp_cmd_set_filter(buf) < 0)
      term_write(E_COLOR_RED "Invalid input\r\n" RESET_ATTRS);
}

/* ----------------------- 'k' edit printk level ----------------------- */

static void edit_printk_level(void)
{
   char buf[16];
   buf[0] = '\0';

   term_move_left(2);
   term_write(E_COLOR_YELLOW "Level [0, 100]: " RESET_ATTRS);
   tui_set_input_blocking(true);
   tui_read_line(buf, sizeof(buf));
   tui_set_input_blocking(false);

   char *endp = NULL;
   long val = strtol(buf, &endp, 10);

   if (!buf[0] || endp == buf || (*endp && *endp != '\0') ||
       val < 0 || val > 100)
   {
      term_write("\r\n");
      term_write(E_COLOR_RED "Invalid input\r\n" RESET_ATTRS);
      return;
   }

   dp_cmd_set_printk_lvl((int)val);
}

/* ---------------------- 't' edit traced PIDs list -------------------- */

/*
 * Parse a comma- or space-separated list of PIDs and mark each via
 * TILCK_CMD_DP_TASK_SET_TRACED. Returns the number of PIDs accepted by
 * the kernel, or -1 on parse error. Master clears the previous list
 * first via dp_cmd_get_traced_tids_and_clear (called from the caller),
 * so this function only needs to apply the new list.
 */
static int parse_and_set_traced_pids(const char *str)
{
   const char *s = str;
   char tok[32];
   int traced_cnt = 0;

   while (1) {

      while (*s == ',' || *s == ' ' || *s == '\t')
         s++;

      if (!*s)
         break;

      size_t i = 0;
      while (*s && *s != ',' && *s != ' ' && *s != '\t' &&
             i + 1 < sizeof(tok))
      {
         tok[i++] = *s++;
      }
      tok[i] = '\0';

      char *endp = NULL;
      long tid = strtol(tok, &endp, 10);

      if (endp == tok || (*endp && *endp != '\0'))
         return -1;

      if (tid <= 0)
         return -1;

      if (dp_cmd_set_task_traced((int)tid, 1) == 0)
         traced_cnt++;
   }

   return traced_cnt;
}

static void edit_traced_pids(void)
{
   /*
    * Master prefills the prompt with the currently-traced TIDs, and
    * the act of building that string also CLEARS the .traced flag on
    * every task. The user then re-types the desired set; missing ones
    * stay cleared, kept ones get re-set. Atomic clear-and-collect
    * lives in TILCK_CMD_DP_TASK_GET_TRACED_TIDS_AND_CLEAR.
    */
   int traced[64];
   long n = dp_cmd_get_traced_tids_and_clear(traced, 64);

   char buf[DP_TRACE_FILTER_MAX];
   buf[0] = '\0';

   if (n > 0) {

      char *p = buf;
      char *end = buf + sizeof(buf);

      for (long i = 0; i < n; i++) {

         int rem = (int)(end - p);
         int rc = snprintf(p, (size_t)rem, "%d,", traced[i]);

         if (rc < 0 || rc >= rem)
            break;

         p += rc;
      }
   }

   term_move_left(2);
   term_write(E_COLOR_YELLOW "PIDs> " RESET_ATTRS);
   tui_set_input_blocking(true);
   tui_read_line(buf, sizeof(buf));
   tui_set_input_blocking(false);

   term_write("\r\n");

   int set = parse_and_set_traced_pids(buf);

   if (set < 0)
      term_write("Invalid input\r\n");
   else
      term_write("Tracing %d tasks\r\n", set);
}

/* ------------------------ live tracing loop -------------------------- */

/*
 * Drive the live tracing loop: read events from /syst/tracing/events
 * and render each one via the kernel renderer. Stops on:
 *
 *   - Ctrl+C — exit the tracer entirely. Returns false. The TTY is
 *     in raw mode (ISIG cleared) so this arrives as byte 0x03 on
 *     stdin, not as a SIGINT signal.
 *   - 'q' typed   — exit. Returns false.
 *   - Enter typed — back to the banner. Returns true.
 *   - I/O error on either fd — exit. Returns false.
 *
 * The kernel-side /syst/tracing/events read is bounded to ~100 ms
 * (KRN_TIMER_HZ / 10) per call so even with no events flowing the
 * stdin probe gets a chance to drain typed commands.
 */
static bool
trace_live_loop(int events_fd)
{
   struct dp_trace_event ev;
   struct dp_render_ctx ctx = {0};
   char rbuf[RENDER_BUF_SZ];
   char c;
   ssize_t n;
   bool keep_banner = false;

   while (1) {

      n = read(STDIN_FILENO, &c, 1);

      if (n == 1) {

         if (c == 'q' || c == TUI_KEY_CTRL_C)
            break;

         if (c == TUI_KEY_ENTER) {
            keep_banner = true;
            break;
         }
      }

      if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
         break;

      n = read(events_fd, &ev, sizeof(ev));

      if (n == (ssize_t)sizeof(ev)) {

         long rc = tr_render_event(&ev, rbuf, sizeof(rbuf), &ctx);

         if (rc > 0)
            term_write_n(rbuf, (int)rc);

         continue;
      }

      if (n < 0 && (errno == EINTR || errno == EAGAIN ||
                    errno == EWOULDBLOCK))
      {
         continue;
      }

      if (n < 0)
         break;

      break;     /* n == 0: treat as EOF */
   }

   if (ctx.last_tp_incomplete_line)
      term_write("\r\n");

   return keep_banner;
}

/*
 * After the live loop exits with "stop tracing" (Enter), drain any
 * events still sitting in the kernel ring buffer. Master prompts
 * "Discard remaining N events in the buf? [Y/n]"; default Y means
 * read-and-drop, n means render them too.
 */
static int dump_remaining_events(int events_fd)
{
   long rem = dp_cmd_get_in_buf_count();

   if (rem <= 0)
      return 0;

   term_write("Discard remaining %ld events in the buf? [Y/n] ",
                rem);

   /* Read a single key; loop until we accept it. */
   char c;
   while (1) {

      ssize_t n = read(STDIN_FILENO, &c, 1);

      if (n == 1) {
         if (c == 'y' || c == 'Y' || c == 'n' || c == 'N' || c == '\r')
            break;
      } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                 errno != EINTR) {
         return -1;
      }
   }

   if (c == '\r' || c == 'Y')
      c = 'y';

   term_write_n(&c, 1);
   term_write("\r\n");

   /* Drain. The events fd's kernel-side read is non-blocking-friendly
    * (returns -EAGAIN when empty), so loop until we hit that or EOF. */
   struct dp_trace_event ev;
   struct dp_render_ctx ctx = {0};
   char rbuf[RENDER_BUF_SZ];

   while (1) {

      ssize_t n = read(events_fd, &ev, sizeof(ev));

      if (n == (ssize_t)sizeof(ev)) {

         if (c == 'n' || c == 'N') {

            long rc = tr_render_event(&ev, rbuf, sizeof(rbuf), &ctx);

            if (rc > 0)
               term_write_n(rbuf, (int)rc);
         }

         continue;
      }

      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
         break;

      if (n < 0 && errno == EINTR)
         continue;

      break;     /* EOF or unexpected error */
   }

   if (ctx.last_tp_incomplete_line)
      term_write("\r\n");

   return 1;
}

/* ----------------------- key dispatcher loop ------------------------- */

/*
 * Single iteration of the tracer's outer command loop: read a key,
 * run the matching action, return whether to keep looping. Shared
 * between the panel-mode and the standalone tracer entry points.
 */
static bool tracer_handle_one_key(int unused)
{
   char c;
   ssize_t rc;

   tui_set_input_blocking(true);
   rc = read(STDIN_FILENO, &c, 1);
   tui_set_input_blocking(false);

   if (rc <= 0)
      return false;

   if (c == 'q' || c == TUI_KEY_CTRL_C)
      return false;

   switch (c) {

      case 'h':
         term_write("%c", c);
         show_help();
         break;

      case 'o':
         term_write("%c", c);
         {
            struct dp_trace_stats st = {0};
            dp_cmd_get_stats(&st);
            dp_cmd_set_force_exp_block(!st.force_exp_block);
         }
         break;

      case 'b':
         term_write("%c", c);
         {
            struct dp_trace_stats st = {0};
            dp_cmd_get_stats(&st);
            dp_cmd_set_dump_big_bufs(!st.dump_big_bufs);
         }
         break;

      case 'e':
         edit_filter();
         break;

      case 'k':
         edit_printk_level();
         break;

      case 'l':
         term_write("%c", c);
         list_traced_syscalls();
         break;

      case 'p':
         term_write("%c", c);
         term_write("\r\n");
         dp_dump_task_list_plain(false);
         break;

      case 'P':
         term_write("%c", c);
         term_write("\r\n");
         dp_dump_task_list_plain(true);
         break;

      case 't':
         edit_traced_pids();
         break;

      case TUI_KEY_ENTER: {

         int events_fd = open(EVENTS_PATH, O_RDONLY | O_NONBLOCK);

         if (events_fd < 0) {
            term_write(E_COLOR_BR_RED
                         "open(%s) failed: errno=%d\r\n"
                         RESET_ATTRS, EVENTS_PATH, errno);
            break;
         }

         term_write("\r\n");
         term_write(E_COLOR_GREEN "-- Tracing active --"
                      RESET_ATTRS "\r\n\r\n");

         dp_cmd_set_enabled(1);
         bool keep_banner = trace_live_loop(events_fd);
         dp_cmd_set_enabled(0);

         if (!keep_banner) {
            close(events_fd);
            return false;     /* clean exit (q or Ctrl+C) */
         }

         term_write(E_COLOR_RED "-- Tracing stopped --"
                      RESET_ATTRS "\r\n");

         if (dump_remaining_events(events_fd) < 0) {
            close(events_fd);
            return false;
         }

         close(events_fd);
         break;
      }

      default:
         /* Unknown key — ignore. */
         return true;
   }

   term_write("\r\n\r\n");
   show_banner();
   return true;
}

/* -------------------- standalone entry point ------------------------- *
 *
 * Previously the tracer also exposed dp_run_tracer_screen(), an
 * in-process entry point dp's Ctrl+T handler called directly.
 * After the tracer was split into its own binary that path was
 * dropped — dp now fork+execs /usr/bin/tracer instead, which lands
 * here.
 */

int dp_run_tracer(void)
{
   tr_meta_init();
   tui_init_layout();
   tui_term_setup();

   /*
    * tui_term_setup hides the cursor (the panel UI doesn't want one),
    * but the tracer's banner ends with a "> " prompt and several
    * commands ('e', 'k', 't') drop into a line editor — those need
    * the cursor visible so the user can see what they're typing.
    * Master called term_cursor_enable(true) on entry to
    * dp_tracing_screen for the same reason; mirror that here.
    * tui_term_restore at exit re-enables it for the host shell.
    */
   term_cursor_enable(true);

   term_clear();
   term_move_cursor(1, 1);
   show_banner();

   while (tracer_handle_one_key(-1))
      ; /* loop */

   dp_cmd_set_enabled(0);
   tui_term_restore();
   return 0;
}
