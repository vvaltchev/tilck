/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Kernel-side string renderer for trace_event records.
 *
 * Master kept the rendering logic in dp_tracing.c + dp_tracing_sys.c
 * and emitted ANSI directly to a kernel TTY handle. In the userspace
 * dp port we keep the same logic — almost verbatim — but write into a
 * sized string buffer (struct sbuf) instead. The userspace dp tool
 * reads one trace_event from /syst/tracing/events, hands it to
 * TILCK_CMD_DP_TRACE_RENDER_EVENT, and writes the resulting string
 * straight to its stdout.
 *
 * Why keep this in-kernel rather than mirror the metadata in
 * userspace: parameter rendering depends on syscall metadata and slot
 * allocations that already live in modules/tracing/. Mirroring them
 * twice would double the surface and risk drift. The render pass
 * itself is pure data → string, so it has no UI concerns; it just
 * happens that the kernel is the one place that knows enough to do it
 * correctly.
 */

#include <tilck_gen_headers/mod_tracing.h>

#if MOD_tracing

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/string_util.h>
#include <tilck/common/dp_abi.h>

#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/user.h>

#include <tilck/kernel/datetime.h>     /* TS_SCALE (ns/sec) */
#include <tilck/mods/tracing.h>

#include "termutil.h"     /* E_COLOR_* / RESET_ATTRS / ATTR_BOLD */

#define REND_BUF_SZ        256

static char *rend_bufs[6];
static int   used_rend_bufs;

void dp_trace_render_init(void);

void dp_trace_render_init(void)
{
   for (int i = 0; i < 6; i++) {

      if (!(rend_bufs[i] = kmalloc(REND_BUF_SZ)))
         panic("[dp] dp_trace_render_init: rend_buf[%d] alloc failed", i);
   }
}

/* ----------------------------- sbuf API ------------------------------- */

struct sbuf {
   char  *buf;
   size_t cap;
   size_t pos;     /* current write offset; never advanced past cap-1 */
};

static void
sbuf_putc(struct sbuf *sb, char c)
{
   if (sb->pos + 1 < sb->cap) {
      sb->buf[sb->pos++] = c;
      sb->buf[sb->pos] = '\0';
   }
}

static void
sbuf_writef(struct sbuf *sb, const char *fmt, ...)
{
   va_list args;
   int rc;

   if (sb->pos + 1 >= sb->cap)
      return;

   va_start(args, fmt);
   rc = vsnprintk(sb->buf + sb->pos, sb->cap - sb->pos, fmt, args);
   va_end(args);

   if (rc < 0)
      return;

   if ((size_t)rc >= sb->cap - sb->pos)
      sb->pos = sb->cap - 1;
   else
      sb->pos += (size_t)rc;
}

static void
sbuf_write_n(struct sbuf *sb, const char *s, int n)
{
   if (n <= 0)
      return;

   if (sb->pos + 1 >= sb->cap)
      return;

   size_t rem = sb->cap - 1 - sb->pos;
   size_t cn  = (size_t)n < rem ? (size_t)n : rem;

   memcpy(sb->buf + sb->pos, s, cn);
   sb->pos += cn;
   sb->buf[sb->pos] = '\0';
}

/* --------------------- syscall parameter rendering -------------------- */

/* Mirrors master's dp_should_full_dump_param. */
static inline bool
should_full_dump_param(bool exp_b,
                       enum sys_param_kind kind,
                       enum trace_event_type t)
{
   return kind == sys_param_in_out ||
          (t == te_sys_enter && kind == sys_param_in) ||
          (t == te_sys_exit && (!exp_b || kind == sys_param_out));
}

static const char *
get_esc_color_for_param(const struct sys_param_type *t, const char *rb)
{
   if (rb[0] == '\"' && t->ui_type == ui_type_string)
      return E_COLOR_RED;

   if (t == &ptype_errno_or_val && rb[0] == '-')
      return E_COLOR_WHITE_ON_RED;

   if (t->ui_type == ui_type_integer)
      return E_COLOR_BR_BLUE;

   return "";
}

static void
dump_rendered_params(struct sbuf *sb,
                     const char *sys_name,
                     const struct syscall_info *si)
{
   int dumped = 0;

   sbuf_writef(sb, "%s(", sys_name);

   for (int i = 0; i < si->n_params; i++) {

      const struct sys_param_info *p = &si->params[i];

      if (!rend_bufs[i][0])
         continue;

      sbuf_writef(sb, E_COLOR_MAGENTA "%s" RESET_ATTRS ": ", p->name);

      sbuf_writef(sb,
                  "%s%s" RESET_ATTRS,
                  get_esc_color_for_param(p->type, rend_bufs[i]),
                  rend_bufs[i]);

      if (dumped < used_rend_bufs - 1)
         sbuf_writef(sb, ", ");

      dumped++;
   }

   sbuf_putc(sb, ')');
}

static void
render_full_dump_single_param(int i,
                              struct trace_event *event,
                              const struct syscall_info *si,
                              const struct sys_param_info *p,
                              const struct sys_param_type *type)
{
   char *data;
   size_t data_size;
   long hlp = -1;     /* helper param: real_size for ptype_buffer */
   struct syscall_event_data *e = &event->sys_ev;

   if (p->helper_param_name) {

      int idx = tracing_get_param_idx(si, p->helper_param_name);
      ASSERT(idx >= 0);

      hlp = (long) e->args[idx];
   }

   if (!tracing_get_slot(event, si, i, &data, &data_size)) {

      ASSERT(type->dump_from_val);

      if (!type->dump_from_val(e->args[i], hlp, rend_bufs[i], REND_BUF_SZ))
         snprintk(rend_bufs[i], REND_BUF_SZ, "(raw) %p", e->args[i]);

   } else {

      long sz = -1;
      ASSERT(type->dump);

      if (p->helper_param_name)
         sz = hlp;

      sz = MIN(sz, (long)data_size);

      if (p->real_sz_in_ret && event->type == te_sys_exit)
         hlp = e->retval >= 0 ? e->retval : 0;

      if (!type->dump(e->args[i], data, sz, hlp, rend_bufs[i], REND_BUF_SZ))
         snprintk(rend_bufs[i], REND_BUF_SZ, "(raw) %p", e->args[i]);
   }
}

static void
render_minimal_dump_single_param(int i, struct trace_event *event)
{
   struct syscall_event_data *e = &event->sys_ev;

   if (!ptype_voidp.dump_from_val(e->args[i], -1, rend_bufs[i], REND_BUF_SZ))
      panic("Unable to serialize a ptype_voidp in a render buf");
}

static void
dump_syscall_with_info(struct sbuf *sb,
                       struct trace_event *e,
                       const char *sys_name,
                       const struct syscall_info *si)
{
   used_rend_bufs = 0;

   for (int i = 0; i < si->n_params; i++) {

      bzero(rend_bufs[i], REND_BUF_SZ);

      const struct sys_param_info *p = &si->params[i];
      const struct sys_param_type *type = p->type;

      if (p->invisible)
         continue;

      if (should_full_dump_param(exp_block(si), p->kind, e->type)) {

         render_full_dump_single_param(i, e, si, p, type);
         used_rend_bufs++;

      } else if (e->type == te_sys_enter) {

         render_minimal_dump_single_param(i, e);
         used_rend_bufs++;
      }
   }

   dump_rendered_params(sb, sys_name, si);
}

static void
dump_ret_val(struct sbuf *sb,
             const struct syscall_info *si,
             long retval)
{
   if (!si) {

      if (retval <= 1024 * 1024) {

         if (retval >= 0) {

            sbuf_writef(sb, E_COLOR_BR_BLUE "%d" RESET_ATTRS, retval);

         } else {

            sbuf_writef(sb,
                        E_COLOR_WHITE_ON_RED "-%s" RESET_ATTRS,
                        get_errno_name(-retval));
         }

      } else {

         sbuf_writef(sb, "%p", retval);
      }

      return;
   }

   const struct sys_param_type *rt = si->ret_type;
   ASSERT(rt->dump_from_val);

   if (!rt->dump_from_val((ulong)retval, -1, rend_bufs[0], REND_BUF_SZ)) {
      sbuf_writef(sb, "(raw) %p", retval);
      return;
   }

   sbuf_writef(sb,
               "%s%s" RESET_ATTRS,
               get_esc_color_for_param(si->ret_type, rend_bufs[0]),
               rend_bufs[0]);
}

static void
dump_syscall_event(struct sbuf *sb,
                   struct trace_event *event,
                   const char *sys_name,
                   const struct syscall_info *si)
{
   struct syscall_event_data *e = &event->sys_ev;

   if (event->type == te_sys_enter) {

      sbuf_writef(sb, E_COLOR_BR_GREEN "ENTER" RESET_ATTRS " ");

   } else {

      if (!si || exp_block(si))
         sbuf_writef(sb, E_COLOR_BR_BLUE "EXIT" RESET_ATTRS " ");
      else
         sbuf_writef(sb, E_COLOR_YELLOW "CALL" RESET_ATTRS " ");
   }

   if (si)
      dump_syscall_with_info(sb, event, sys_name, si);
   else
      sbuf_writef(sb, "%s()", sys_name);

   if (event->type == te_sys_exit) {

      sbuf_writef(sb, " -> ");
      dump_ret_val(sb, si, e->retval);
   }

   sbuf_writef(sb, "\r\n");
}

static void
handle_syscall_event(struct sbuf *sb, struct trace_event *e)
{
   const char *sys_name;
   const struct syscall_info *si;
   struct syscall_event_data *se = &e->sys_ev;

   sys_name = tracing_get_syscall_name(se->sys);
   ASSERT(sys_name);
   sys_name += 4;     /* skip the "sys_" prefix */
   si = tracing_get_syscall_info(se->sys);
   dump_syscall_event(sb, e, sys_name, si);
}

/* ----------------------- printk event rendering ----------------------- */

static void
dump_event_prefix(struct sbuf *sb, struct trace_event *e)
{
   sbuf_writef(sb,
               "%05u.%03u [%05d] ",
               (u32)(e->sys_time / TS_SCALE),
               (u32)((e->sys_time % TS_SCALE) / (TS_SCALE / 1000)),
               e->tid);
}

static void
dump_trace_printk_event(struct sbuf *sb,
                        struct trace_event *e,
                        struct dp_render_ctx *ctx)
{
   const char default_trunc_str[] = TRACE_PRINTK_TRUNC_STR;
   const size_t trunc_str_len = sizeof(default_trunc_str) - 1;
   size_t max_len = sizeof(e->p_ev.buf) - 1;
   const char *buf = e->p_ev.buf;
   const char *endln = "";
   const char *trunc = "";
   const char *log_color = "";
   size_t len = 0;
   bool continuation = false;

   if (*buf == '\n' && !ctx->last_tp_incomplete_line) {
      buf++;
      max_len--;
   }

   while (len < max_len && buf[len] != '\0') {
      len++;
   }

   if (buf[len] != '\0') {
      ASSERT(len == max_len);
      trunc = E_COLOR_BR_RED TRACE_PRINTK_TRUNC_STR RESET_ATTRS;
   }

   if (UNLIKELY(len == 0))
      return;

   if (ctx->last_tp_incomplete_line) {

      if (ctx->last_tp_tid == e->tid &&
          ctx->last_tp_sys_time == e->sys_time &&
          ctx->last_tp_in_irq == (e->p_ev.in_irq ? 1 : 0))
      {
         continuation = true;

      } else {

         bzero(ctx, sizeof(*ctx));
         sbuf_writef(sb, "\r\n");
      }
   }

   if (buf[len - 1] == '\n') {

      bzero(ctx, sizeof(*ctx));
      endln = "\r";

   } else if (trunc[0] == '\0') {

      ctx->last_tp_incomplete_line = 1;
      ctx->last_tp_sys_time = e->sys_time;
      ctx->last_tp_tid = e->tid;
      ctx->last_tp_in_irq = e->p_ev.in_irq ? 1 : 0;
   }

   if (len >= trunc_str_len &&
       !strcmp(buf + len - trunc_str_len, TRACE_PRINTK_TRUNC_STR))
   {
      len -= trunc_str_len;
      trunc = E_COLOR_BR_RED TRACE_PRINTK_TRUNC_STR RESET_ATTRS;
   }

   if (trunc[0] != '\0') {
      bzero(ctx, sizeof(*ctx));
      endln = "\r\n";
   }

   if (len >= 4 && !strncmp(buf, "*** ", 4)) {
      log_color = ATTR_BOLD;
   }

   if (continuation) {

      sbuf_writef(sb, E_COLOR_MAGENTA "%s", log_color);
      sbuf_write_n(sb, buf, (int)len);
      sbuf_writef(sb, "%s%s" RESET_ATTRS, trunc, endln);

   } else {

      dump_event_prefix(sb, e);
      sbuf_writef(sb,
                  E_COLOR_YELLOW "LOG" RESET_ATTRS "[%02d]: %s",
                  e->p_ev.level, log_color);
      sbuf_write_n(sb, buf, (int)len);
      sbuf_writef(sb, "%s%s" RESET_ATTRS, trunc, endln);
   }
}

/* ------------------------- top-level renderer ------------------------- */

static void
dump_tracing_event(struct sbuf *sb,
                   struct trace_event *e,
                   struct dp_render_ctx *ctx)
{
   if (e->type != te_printk) {
      /* For trace_printk events the prefix is emitted only when the
       * event isn't a continuation; everything else gets it
       * unconditionally. */
      dump_event_prefix(sb, e);
   }

   switch (e->type) {

      case te_sys_enter:
      case te_sys_exit:
         handle_syscall_event(sb, e);
         break;

      case te_printk:
         dump_trace_printk_event(sb, e, ctx);
         break;

      case te_signal_delivered:
         sbuf_writef(sb,
                     E_COLOR_YELLOW "GOT SIGNAL: " RESET_ATTRS "%s[%d]\r\n",
                     get_signal_name(e->sig_ev.signum),
                     e->sig_ev.signum);
         break;

      case te_killed:
         sbuf_writef(sb,
                     E_COLOR_BR_RED "KILLED BY SIGNAL: "
                     RESET_ATTRS "%s[%d]\r\n",
                     get_signal_name(e->sig_ev.signum),
                     e->sig_ev.signum);
         break;

      default:
         sbuf_writef(sb,
                     E_COLOR_BR_RED "<unknown event %d>\r\n" RESET_ATTRS,
                     e->type);
   }
}

/*
 * Public entry point used by dp_data.c::tilck_sys_dp_trace_render_event.
 * Renders a single trace event into `out`/`out_sz` and returns the
 * number of bytes written. Always NUL-terminates if there is room.
 *
 * The userspace dp tool calls this once per event; ctx is read-modify-
 * write so the printk continuation logic survives across calls.
 */
int
dp_trace_render_event(struct trace_event *e,
                      char *out,
                      size_t out_sz,
                      struct dp_render_ctx *ctx)
{
   struct sbuf sb = { .buf = out, .cap = out_sz, .pos = 0 };
   struct dp_render_ctx local_ctx = {0};

   if (out_sz < 2)
      return -EINVAL;

   if (!ctx)
      ctx = &local_ctx;

   /* NUL-terminate up front so partial output is still a valid C
    * string if rendering bails halfway. */
   out[0] = '\0';

   dump_tracing_event(&sb, e, ctx);

   return (int)sb.pos;
}

#endif /* MOD_tracing */
