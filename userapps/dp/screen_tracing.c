/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Userspace tracer screen — full-screen mode entered via `tracer` or
 * `dp -t`. Replaces the in-kernel modules/debugpanel/dp_tracing.c TUI
 * with a stdin/stdout-driven version that:
 *
 *   - Pulls runtime state via TILCK_CMD_DP_TRACE_GET_STATS /
 *     TILCK_CMD_DP_TRACE_GET_FILTER for the banner.
 *   - Edits the syscall filter via TILCK_CMD_DP_TRACE_SET_FILTER
 *     using the existing dp_read_line() line editor.
 *   - Toggles the kernel-side tracing flag via
 *     TILCK_CMD_DP_TRACE_SET_ENABLED on Enter / 'q'.
 *   - Reads one struct dp_trace_event per read() from
 *     /syst/tracing/events while tracing is active.
 *   - Renders syscall enter/exit, printk, signal-delivered, and
 *     killed events. Syscall names are looked up lazily via
 *     TILCK_CMD_DP_TRACE_GET_SYS_NAME and cached.
 *
 * Deferred (tracked for future work, not in this MVP): per-parameter
 * rendering (would need to mirror modules/tracing/tracing_metadata.c),
 * trace_printk multi-line continuation logic, edit-traced-PIDs ('t'),
 * edit-printk-level ('k'), force-exp-block toggle ('o'),
 * dump-big-bufs toggle ('b'), list-traced-syscalls ('l'), task-list
 * dump ('p'/'P'), discard-remaining-events prompt.
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

#include "termutil.h"
#include "dp_int.h"

#define TRACER_TS_SCALE          1000000000ULL    /* ns per second */
#define TRACER_SYS_NAME_CACHE    512              /* MAX_SYSCALLS upper bound */

/* Cache of syscall names: NULL = not yet fetched, "" = no name. */
static char *sys_name_cache[TRACER_SYS_NAME_CACHE];

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
dp_cmd_get_sys_name(unsigned sys_n, char *buf, unsigned long buf_sz)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TRACE_GET_SYS_NAME,
                  (long)sys_n, (long)buf, (long)buf_sz, 0L);
}

/*
 * Fetch the syscall name for `sys_n`, caching the result. Strips the
 * "sys_" prefix the kernel uses internally (so "sys_read" → "read").
 * Returns a pointer to the cached string; never NULL — falls back to
 * a synthesized "syscall_<n>" if the kernel couldn't resolve it.
 */
static const char *
tracer_sys_name(unsigned sys_n)
{
   if (sys_n >= TRACER_SYS_NAME_CACHE) {
      static char fallback[32];
      snprintf(fallback, sizeof(fallback), "syscall_%u", sys_n);
      return fallback;
   }

   if (sys_name_cache[sys_n])
      return sys_name_cache[sys_n];

   char buf[DP_SYS_NAME_MAX];
   long rc = dp_cmd_get_sys_name(sys_n, buf, sizeof(buf));

   const char *src;
   if (rc < 0) {
      static char fallback[32];
      snprintf(fallback, sizeof(fallback), "syscall_%u", sys_n);
      src = fallback;
   } else {
      src = buf;
      /* Strip "sys_" prefix if present. */
      if (!strncmp(src, "sys_", 4))
         src += 4;
   }

   sys_name_cache[sys_n] = strdup(src);
   return sys_name_cache[sys_n] ? sys_name_cache[sys_n] : "?";
}

/* ----------------------------- banner UI ----------------------------- */

static void show_banner(void)
{
   struct dp_trace_stats st = {0};
   char filter[DP_TRACE_FILTER_MAX];
   long rc;

   if (dp_cmd_get_stats(&st) < 0) {
      dp_write_raw(E_COLOR_BR_RED
                   "TILCK_CMD_DP_TRACE_GET_STATS failed (errno=%d)\r\n"
                   RESET_ATTRS, errno);
      return;
   }

   filter[0] = '\0';
   rc = dp_cmd_get_filter(filter, sizeof(filter));
   if (rc < 0)
      strcpy(filter, "?");

   dp_write_raw(E_COLOR_YELLOW
                "Tilck syscall tracing (h: help)\r\n"
                RESET_ATTRS);

   dp_write_raw(
      TERM_VLINE " Always ENTER+EXIT: %s "
      TERM_VLINE " Big bufs: %s "
      TERM_VLINE " #Sys traced: " E_COLOR_BR_BLUE "%d" RESET_ATTRS " "
      TERM_VLINE " #Tasks traced: " E_COLOR_BR_BLUE "%d" RESET_ATTRS " "
      TERM_VLINE "\r\n"
      TERM_VLINE " Printk lvl: " E_COLOR_BR_BLUE "%d" RESET_ATTRS "\r\n",
      st.force_exp_block ? E_COLOR_GREEN "ON" RESET_ATTRS
                         : E_COLOR_RED "OFF" RESET_ATTRS,
      st.dump_big_bufs   ? E_COLOR_GREEN "ON" RESET_ATTRS
                         : E_COLOR_RED "OFF" RESET_ATTRS,
      st.sys_traced_count,
      st.tasks_traced_count,
      st.printk_lvl);

   dp_write_raw(TERM_VLINE " Trace expr: " E_COLOR_YELLOW "%s" RESET_ATTRS,
                filter);

   dp_write_raw("\r\n");
   dp_write_raw(E_COLOR_YELLOW "> " RESET_ATTRS);
}

static void show_help(void)
{
   dp_write_raw("\r\n\r\n"
                E_COLOR_YELLOW "Tracing mode help" RESET_ATTRS "\r\n"

                "  " E_COLOR_YELLOW "h" RESET_ATTRS
                "     : This help screen\r\n"

                "  " E_COLOR_YELLOW "e" RESET_ATTRS
                "     : Edit syscalls wildcard expr "
                E_COLOR_RED "[1]" RESET_ATTRS "\r\n"

                "  " E_COLOR_YELLOW "t" RESET_ATTRS
                "     : Edit comma-separated list of traced PIDs\r\n"

                "  " E_COLOR_YELLOW "q" RESET_ATTRS
                "     : Quit the tracer\r\n"

                "  " RESET_ATTRS "ENTER : Start / stop tracing\r\n"

                "\r\n"
                E_COLOR_RED "[1]" RESET_ATTRS
                " In the wildcard expr the "
                E_COLOR_BR_WHITE "*" RESET_ATTRS
                " character is allowed only once,\r\n"
                "    at the end. The "
                E_COLOR_BR_WHITE "!" RESET_ATTRS
                " character can be used at the start of\r\n"
                "    each sub-expr to negate it. Sub-exprs are"
                " separated by comma\r\n"
                "    or space. Example: "
                E_COLOR_BR_WHITE "read*,write*,!readlink*" RESET_ATTRS "\r\n");
}

static void edit_filter(void)
{
   char buf[DP_TRACE_FILTER_MAX];
   buf[0] = '\0';

   dp_cmd_get_filter(buf, sizeof(buf));

   dp_move_left(2);
   dp_write_raw(E_COLOR_YELLOW "expr> " RESET_ATTRS);
   dp_set_input_blocking(true);
   dp_read_line(buf, sizeof(buf));
   dp_set_input_blocking(false);

   if (dp_cmd_set_filter(buf) < 0)
      dp_write_raw(E_COLOR_RED "Invalid input\r\n" RESET_ATTRS);
}

static long
dp_cmd_set_task_traced(int tid, int enabled)
{
   return syscall(TILCK_CMD_SYSCALL,
                  TILCK_CMD_DP_TASK_SET_TRACED,
                  (long)tid, (long)enabled, 0L, 0L);
}

/*
 * Parse a comma- or space-separated list of PIDs and mark each via
 * TILCK_CMD_DP_TASK_SET_TRACED. Returns the number of PIDs accepted
 * by the kernel, or -1 on parse error.
 *
 * Note: this is a simple "set the listed tasks as traced" — it does
 * NOT clear previously-traced tasks first. The kernel TUI used to do
 * the clear, but in our split design that would require a sweep
 * iterator we haven't exposed yet. Defer; for the MVP user can edit
 * the list incrementally.
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
   char buf[DP_TRACE_FILTER_MAX];
   buf[0] = '\0';

   dp_move_left(2);
   dp_write_raw(E_COLOR_YELLOW "PIDs> " RESET_ATTRS);
   dp_set_input_blocking(true);
   dp_read_line(buf, sizeof(buf));
   dp_set_input_blocking(false);

   dp_write_raw("\r\n");

   int n = parse_and_set_traced_pids(buf);

   if (n < 0)
      dp_write_raw(E_COLOR_RED "Invalid input\r\n" RESET_ATTRS);
   else
      dp_write_raw("Tracing %d task(s)\r\n", n);
}

/* --------------------------- event rendering ------------------------- */

static void
render_event_prefix(const struct dp_trace_event *e)
{
   const unsigned sec  = (unsigned)(e->sys_time / TRACER_TS_SCALE);
   const unsigned msec = (unsigned)((e->sys_time % TRACER_TS_SCALE) /
                                    (TRACER_TS_SCALE / 1000));
   dp_write_raw("%05u.%03u [%05d] ", sec, msec, e->tid);
}

static void
render_syscall_event(const struct dp_trace_event *e)
{
   const struct dp_syscall_event_data *s = &e->sys_ev;
   const char *name = tracer_sys_name(s->sys);

   if (e->type == dp_te_sys_enter) {
      dp_write_raw(E_COLOR_BR_GREEN "ENTER" RESET_ATTRS " %s()\r\n", name);
      return;
   }

   /* dp_te_sys_exit */
   dp_write_raw(E_COLOR_BR_BLUE "EXIT " RESET_ATTRS " %s() -> ", name);

   if (s->retval >= 0) {
      dp_write_raw(E_COLOR_BR_BLUE "%ld" RESET_ATTRS, s->retval);
   } else {
      /* Negative — likely an errno. Render as -<n> in red. */
      dp_write_raw(E_COLOR_WHITE_ON_RED "%ld" RESET_ATTRS, s->retval);
   }
   dp_write_raw("\r\n");
}

static void
render_printk_event(const struct dp_trace_event *e)
{
   const struct dp_printk_event_data *p = &e->p_ev;
   const char *buf = p->buf;
   size_t len = 0;

   /* Skip a single leading newline (mirrors kernel-side behavior). */
   if (*buf == '\n') {
      buf++;
   }

   /* Bounded strnlen */
   while (len < DP_PRINTK_BUF_SIZE - 1 && buf[len] != '\0')
      len++;

   if (len == 0)
      return;

   dp_write_raw(E_COLOR_YELLOW "LOG" RESET_ATTRS "[%02d]: ", p->level);
   dp_write_raw_int(buf, (int)len);

   if (buf[len - 1] != '\n')
      dp_write_raw("\r\n");
   else
      dp_write_raw("\r");
}

static void
render_signal_event(const struct dp_trace_event *e, bool killed)
{
   const struct dp_signal_event_data *s = &e->sig_ev;

   if (killed) {
      dp_write_raw(E_COLOR_BR_RED "KILLED BY SIGNAL: "
                   RESET_ATTRS "[%d]\r\n", s->signum);
   } else {
      dp_write_raw(E_COLOR_YELLOW "GOT SIGNAL: "
                   RESET_ATTRS "[%d]\r\n", s->signum);
   }
}

static void
render_event(const struct dp_trace_event *e)
{
   if (e->type != dp_te_printk)
      render_event_prefix(e);

   switch (e->type) {

      case dp_te_sys_enter:
      case dp_te_sys_exit:
         render_syscall_event(e);
         break;

      case dp_te_printk:
         render_printk_event(e);
         break;

      case dp_te_signal_delivered:
         render_signal_event(e, false);
         break;

      case dp_te_killed:
         render_signal_event(e, true);
         break;

      default:
         dp_write_raw(E_COLOR_BR_RED "<unknown event type %d>\r\n"
                      RESET_ATTRS, e->type);
         break;
   }
}

/* ------------------------ tracing live mode -------------------------- */

/*
 * Drive the live tracing loop: read events from /syst/tracing/events
 * and render each one. Stops on:
 *
 *   - Ctrl+C — exit the tracer entirely. Returns false. The TTY is
 *     in raw mode (ISIG cleared) so this arrives as byte 0x03 on
 *     stdin, not as a SIGINT signal.
 *
 *   - 'q' typed   — exit. Returns false.
 *   - Enter typed — back to the banner. Returns true.
 *
 * These rely on the inter-event stdin probe getting a chance: while
 * the trace is idle the events read is parked in a kernel-side
 * kcond_wait, so a typed byte stays buffered until the next event
 * lands. Spawn at least one traced task that does some work and the
 * keys come back fast.
 *
 *   - I/O error on either fd — exit. Returns false.
 */
static bool
trace_live_loop(int events_fd)
{
   struct dp_trace_event ev;
   char c;
   ssize_t n;
   bool keep_banner = false;

   /* stdin is already in non-blocking mode (set by dp_term_setup). */

   while (1) {

      /* Cheap non-blocking stdin probe — this is the ONLY exit path
       * once events start flowing, since the kernel side parks in
       * kcond_wait while idle and signals don't fire because the
       * raw-mode TTY swallows ISIG. */
      n = read(STDIN_FILENO, &c, 1);

      if (n == 1) {

         if (c == 'q' || c == DP_KEY_CTRL_C)
            break;

         if (c == DP_KEY_ENTER) {
            keep_banner = true;
            break;
         }
      }

      if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
         break;     /* unexpected I/O error on stdin */

      n = read(events_fd, &ev, sizeof(ev));

      if (n == (ssize_t)sizeof(ev)) {
         render_event(&ev);
         continue;
      }

      if (n < 0 && (errno == EINTR || errno == EAGAIN ||
                    errno == EWOULDBLOCK))
      {
         continue;
      }

      if (n < 0)
         break;     /* unexpected I/O error on events fd */

      /* n == 0 (shouldn't happen on a stream): treat as EOF. */
      break;
   }

   return keep_banner;
}

/* -------------------------- main entry point ------------------------- */

int dp_run_tracer(void)
{
   int events_fd;
   char c;

   dp_init_layout();
   dp_term_setup();

   dp_clear();
   dp_move_cursor(1, 1);
   show_banner();

   while (1) {

      /* Block on stdin for command keys outside the live loop. */
      dp_set_input_blocking(true);
      ssize_t rc = read(STDIN_FILENO, &c, 1);
      dp_set_input_blocking(false);

      if (rc <= 0)
         break;

      if (c == 'q')
         break;

      if (c == 'h') {
         dp_write_raw("%c", c);
         show_help();
         dp_write_raw("\r\n");
         show_banner();
         continue;
      }

      if (c == 'e') {
         edit_filter();
         dp_write_raw("\r\n");
         show_banner();
         continue;
      }

      if (c == 't') {
         edit_traced_pids();
         dp_write_raw("\r\n");
         show_banner();
         continue;
      }

      if (c == DP_KEY_ENTER) {

         events_fd = open("/syst/tracing/events", O_RDONLY | O_NONBLOCK);

         if (events_fd < 0) {
            dp_write_raw(E_COLOR_BR_RED
                         "open(/syst/tracing/events) failed: errno=%d\r\n"
                         RESET_ATTRS, errno);
            show_banner();
            continue;
         }

         dp_write_raw("\r\n" E_COLOR_GREEN "-- Tracing active --"
                      RESET_ATTRS " (Ctrl+C to stop)\r\n\r\n");

         dp_cmd_set_enabled(1);
         bool keep_banner = trace_live_loop(events_fd);
         dp_cmd_set_enabled(0);

         close(events_fd);

         if (!keep_banner)
            break;

         dp_write_raw("\r\n" E_COLOR_RED "-- Tracing stopped --"
                      RESET_ATTRS "\r\n\r\n");
         show_banner();
         continue;
      }

      /* Unknown key — ignore. */
   }

   /* Make sure tracing is off when we leave. */
   dp_cmd_set_enabled(0);

   dp_term_restore();
   return 0;
}
