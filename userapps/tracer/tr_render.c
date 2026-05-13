/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Userspace port of modules/debugpanel/dp_trace_render.c.
 *
 * The state machine is the same as the kernel renderer:
 *
 *   - syscall events: prefix → ENTER/EXIT/CALL → name(p1: val,
 *     p2: val, ...) [-> retval]
 *   - trace_printk events: prefix → LOG[lvl]: text, with multi-line
 *     continuation, "{...}" truncation marker, leading-newline skip,
 *     and "*** " bold detection
 *   - signal_delivered / killed: prefix → "GOT/KILLED BY SIGNAL: " +
 *     name + signum
 *
 * The structural difference vs the kernel version: where the kernel
 * called `t->dump(...)` / `t->dump_from_val(...)` through function
 * pointers on a `struct sys_param_type`, we dispatch by `type_id`
 * via tr_dump.c. The metadata (struct tr_wire_syscall) comes from
 * /syst/tracing/metadata which the kernel built from the same
 * ptype + slot tables, so the per-param choices are identical.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tilck/common/tracing/wire.h>
#include <tilck/common/dp_abi.h>

#include "term.h"
#include "tr.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define REND_BUF_SZ          256
#define TS_SCALE_NS          1000000000ULL
#define TRACE_PRINTK_TRUNC   "{...}"

/* ----------------------------- sbuf API ------------------------------- */

struct sbuf {
   char  *buf;
   size_t cap;
   size_t pos;     /* current write offset; never advanced past cap-1 */
};

static void sbuf_putc(struct sbuf *sb, char c)
{
   if (sb->pos + 1 < sb->cap) {
      sb->buf[sb->pos++] = c;
      sb->buf[sb->pos] = '\0';
   }
}

static void sbuf_writef(struct sbuf *sb, const char *fmt, ...)
{
   va_list args;

   if (sb->pos + 1 >= sb->cap)
      return;

   va_start(args, fmt);
   const int rc = vsnprintf(sb->buf + sb->pos, sb->cap - sb->pos, fmt, args);
   va_end(args);

   if (rc < 0)
      return;

   if ((size_t)rc >= sb->cap - sb->pos)
      sb->pos = sb->cap - 1;
   else
      sb->pos += (size_t)rc;
}

static void sbuf_write_n(struct sbuf *sb, const char *s, int n)
{
   if (n <= 0)
      return;

   if (sb->pos + 1 >= sb->cap)
      return;

   const size_t rem = sb->cap - 1 - sb->pos;
   const size_t cn  = (size_t)n < rem ? (size_t)n : rem;

   memcpy(sb->buf + sb->pos, s, cn);
   sb->pos += cn;
   sb->buf[sb->pos] = '\0';
}

/* --------------------- per-event saved-data slot lookup --------------- */

/*
 * Compute (data ptr, data size) for the i-th param of an event
 * given the wire-format syscall info. Returns false if the param has
 * no allocated slot — in that case the caller falls back to the
 * dump_from_val path.
 */
static bool get_slot_data(const struct dp_trace_event *e,
                          const struct tr_wire_syscall *si,
                          int p_idx,
                          char **data,
                          size_t *size)
{
   const s8 slot = si->slots[p_idx];

   if (slot < 0)
      return false;

   const u8 fmt = si->fmt;

   if (fmt > 1 || slot > 3)
      return false;

   /* The userspace dp_syscall_event_data has saved_params[176] as
    * an opaque blob starting where the kernel's anonymous fmt0/fmt1
    * union starts; the byte offsets within saved_params are the
    * same as the kernel's relative-within-union offsets. */
   *data = ((char *)e->sys_ev.saved_params) + tr_fmt_offsets[fmt][slot];
   *size = tr_fmt_sizes[fmt][slot];

   return true;
}

/* exp_block(si) equivalent: kernel's exp_block reads the kernel
 * global __force_exp_block; userspace mirrors it via the local
 * g_force_exp_block flag (set from screen_tracing.c after every
 * dp_cmd_get_stats refresh). */
static bool exp_block_eff(const struct tr_wire_syscall *si)
{
   return tr_get_force_exp_block() || (si && si->exp_block);
}

/* --------------------- syscall parameter rendering -------------------- */

/* Per-render-call scratch space. Keeping these as static globals
 * mirrors the kernel renderer; the renderer is single-threaded. */
static char rend_bufs[6][REND_BUF_SZ];
static int  used_rend_bufs;

/* Mirrors master's dp_should_full_dump_param. */
static inline bool should_full_dump_param(bool exp_b,
                                          unsigned kind,
                                          int event_type)
{
   return kind == TR_KIND_IN_OUT ||
          (event_type == dp_te_sys_enter && kind == TR_KIND_IN) ||
          (event_type == dp_te_sys_exit  &&
              (!exp_b || kind == TR_KIND_OUT));
}

/* Color rule (port of get_esc_color_for_param). Takes the type id +
 * the rendered string + the ptype's ui_type. */
static const char *get_esc_color(unsigned type_id, unsigned ui_type,
                                 const char *rb)
{
   if (rb[0] == '\"' && ui_type == TR_UI_STRING)
      return E_COLOR_RED;

   if (type_id == TR_PT_ERRNO_OR_VAL && rb[0] == '-')
      return E_COLOR_WHITE_ON_RED;

   if (ui_type == TR_UI_INTEGER)
      return E_COLOR_BR_BLUE;

   return "";
}

static unsigned ptype_ui_type(unsigned type_id)
{
   const struct tr_wire_ptype_info *pi = tr_get_ptype_info(type_id);
   return pi ? pi->ui_type : TR_UI_OTHER;
}

static void dump_rendered_params(struct sbuf *sb,
                                 const char *sys_name,
                                 const struct tr_wire_syscall *si)
{
   int dumped = 0;

   sbuf_writef(sb, "%s(", sys_name);

   for (int i = 0; i < si->n_params; i++) {

      const struct tr_wire_param *p = &si->params[i];

      if (!rend_bufs[i][0])
         continue;

      sbuf_writef(sb, E_COLOR_MAGENTA "%s" RESET_ATTRS ": ", p->name);

      sbuf_writef(sb,
                  "%s%s" RESET_ATTRS,
                  get_esc_color(p->type_id, ptype_ui_type(p->type_id),
                                rend_bufs[i]),
                  rend_bufs[i]);

      if (dumped < used_rend_bufs - 1)
         sbuf_writef(sb, ", ");

      dumped++;
   }

   sbuf_putc(sb, ')');
}

static void render_full_dump_single_param(int i,
                                          const struct dp_trace_event *e,
                                          const struct tr_wire_syscall *si,
                                          const struct tr_wire_param *p)
{
   char *data = NULL;
   size_t data_size = 0;
   long hlp = -1;
   const struct dp_syscall_event_data *se = &e->sys_ev;

   if (p->helper_idx >= 0 && p->helper_idx < si->n_params)
      hlp = (long)se->args[p->helper_idx];

   if (!get_slot_data(e, si, i, &data, &data_size)) {

      if (!tr_dump_from_val(p->type_id, se->args[i], hlp,
                            rend_bufs[i], REND_BUF_SZ))
      {
         snprintf(rend_bufs[i], REND_BUF_SZ, "(raw) %p",
                  (void *)se->args[i]);
      }

   } else {

      long sz = -1;

      if (p->helper_idx >= 0)
         sz = hlp;

      sz = MIN(sz, (long)data_size);

      if (p->real_sz_in_ret && e->type == dp_te_sys_exit)
         hlp = se->retval >= 0 ? se->retval : 0;

      if (!tr_dump(p->type_id, se->args[i], data, sz, hlp,
                   rend_bufs[i], REND_BUF_SZ))
      {
         snprintf(rend_bufs[i], REND_BUF_SZ, "(raw) %p",
                  (void *)se->args[i]);
      }
   }
}

static void render_minimal_dump_single_param(int i,
                                             const struct dp_trace_event *e)
{
   const struct dp_syscall_event_data *se = &e->sys_ev;

   /* Equivalent of kernel's ptype_voidp.dump_from_val on the raw
    * arg value. */
   if (!tr_dump_from_val(TR_PT_VOIDP, se->args[i], -1,
                         rend_bufs[i], REND_BUF_SZ))
   {
      snprintf(rend_bufs[i], REND_BUF_SZ, "(raw) %p",
               (void *)se->args[i]);
   }
}

static void dump_syscall_with_info(struct sbuf *sb,
                                   const struct dp_trace_event *e,
                                   const char *sys_name,
                                   const struct tr_wire_syscall *si)
{
   used_rend_bufs = 0;

   for (int i = 0; i < si->n_params; i++) {

      memset(rend_bufs[i], 0, REND_BUF_SZ);

      const struct tr_wire_param *p = &si->params[i];

      if (p->invisible)
         continue;

      if (should_full_dump_param(exp_block_eff(si), p->kind, e->type)) {

         render_full_dump_single_param(i, e, si, p);
         used_rend_bufs++;

      } else if (e->type == dp_te_sys_enter) {

         render_minimal_dump_single_param(i, e);
         used_rend_bufs++;
      }
   }

   dump_rendered_params(sb, sys_name, si);
}

static void dump_ret_val(struct sbuf *sb,
                         const struct tr_wire_syscall *si,
                         long retval)
{
   if (!si) {

      if (retval <= 1024 * 1024) {

         if (retval >= 0) {

            sbuf_writef(sb, E_COLOR_BR_BLUE "%ld" RESET_ATTRS, retval);

         } else {

            sbuf_writef(sb,
                        E_COLOR_WHITE_ON_RED "-%s" RESET_ATTRS,
                        tr_get_errno_name((int)-retval));
         }

      } else {

         sbuf_writef(sb, "%p", (void *)retval);
      }

      return;
   }

   if (!tr_dump_from_val(si->ret_type_id, (unsigned long)retval, -1,
                         rend_bufs[0], REND_BUF_SZ))
   {
      sbuf_writef(sb, "(raw) %p", (void *)retval);
      return;
   }

   sbuf_writef(sb,
               "%s%s" RESET_ATTRS,
               get_esc_color(si->ret_type_id,
                             ptype_ui_type(si->ret_type_id),
                             rend_bufs[0]),
               rend_bufs[0]);
}

/* --------------------------- syscall name --------------------------- */

/*
 * Lazy cache for syscall names. Userspace fetches each name once via
 * TILCK_CMD_DP_TRACE_GET_SYS_NAME (already wrapped in screen_tracing.c
 * as dp_cmd_get_sys_name); this renderer was previously calling the
 * kernel-side tracing_get_syscall_name. We keep the lazy cache here
 * so render_event() can be self-contained.
 */
#include <sys/syscall.h>
#include <unistd.h>
#include <tilck/common/syscalls.h>

#define MAX_SYS_NAMES   512

static char *sys_name_cache[MAX_SYS_NAMES];

static const char *get_syscall_name_cached(unsigned sys_n)
{
   if (sys_n >= MAX_SYS_NAMES) {
      static char fallback[32];
      snprintf(fallback, sizeof(fallback), "syscall_%u", sys_n);
      return fallback;
   }

   if (sys_name_cache[sys_n])
      return sys_name_cache[sys_n];

   char buf[64];
   long rc = syscall(TILCK_CMD_SYSCALL,
                     TILCK_CMD_DP_TRACE_GET_SYS_NAME,
                     (long)sys_n, (long)buf, (long)sizeof(buf), 0L);

   const char *src;

   if (rc < 0) {
      static char fallback[32];
      snprintf(fallback, sizeof(fallback), "syscall_%u", sys_n);
      src = fallback;
   } else {
      src = buf;
      /* Strip leading "sys_" — master's render did the same. */
      if (!strncmp(src, "sys_", 4))
         src += 4;
   }

   sys_name_cache[sys_n] = strdup(src);
   return sys_name_cache[sys_n] ? sys_name_cache[sys_n] : "?";
}

static void dump_syscall_event(struct sbuf *sb,
                               const struct dp_trace_event *e,
                               const char *sys_name,
                               const struct tr_wire_syscall *si)
{
   const struct dp_syscall_event_data *se = &e->sys_ev;

   if (e->type == dp_te_sys_enter) {

      sbuf_writef(sb, E_COLOR_BR_GREEN "ENTER" RESET_ATTRS " ");

   } else {

      if (!si || exp_block_eff(si))
         sbuf_writef(sb, E_COLOR_BR_BLUE "EXIT" RESET_ATTRS " ");
      else
         sbuf_writef(sb, E_COLOR_YELLOW "CALL" RESET_ATTRS " ");
   }

   if (si)
      dump_syscall_with_info(sb, e, sys_name, si);
   else
      sbuf_writef(sb, "%s()", sys_name);

   if (e->type == dp_te_sys_exit) {

      sbuf_writef(sb, " -> ");
      dump_ret_val(sb, si, se->retval);
   }

   sbuf_writef(sb, "\r\n");
}

static void handle_syscall_event(struct sbuf *sb,
                                 const struct dp_trace_event *e)
{
   const struct dp_syscall_event_data *se = &e->sys_ev;
   const char *sys_name = get_syscall_name_cached(se->sys);
   const struct tr_wire_syscall *si = tr_get_sys_info(se->sys);

   dump_syscall_event(sb, e, sys_name, si);
}

/* ----------------------- printk event rendering ----------------------- */

static void dump_event_prefix(struct sbuf *sb, const struct dp_trace_event *e)
{
   sbuf_writef(sb,
               "%05u.%03u [%05d] ",
               (unsigned)(e->sys_time / TS_SCALE_NS),
               (unsigned)((e->sys_time % TS_SCALE_NS) /
                          (TS_SCALE_NS / 1000)),
               e->tid);
}

static void dump_trace_printk_event(struct sbuf *sb,
                                    const struct dp_trace_event *e,
                                    struct dp_render_ctx *ctx)
{
   const char trunc_str[] = TRACE_PRINTK_TRUNC;
   const size_t trunc_str_len = sizeof(trunc_str) - 1;
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

   while (len < max_len && buf[len] != '\0')
      len++;

   if (buf[len] != '\0') {
      /* truncated (no NUL within the slot): render in-place tail */
      trunc = E_COLOR_BR_RED TRACE_PRINTK_TRUNC RESET_ATTRS;
   }

   if (len == 0)
      return;

   if (ctx->last_tp_incomplete_line) {

      if (ctx->last_tp_tid == e->tid &&
          ctx->last_tp_sys_time == e->sys_time &&
          ctx->last_tp_in_irq == (e->p_ev.in_irq ? 1 : 0))
      {
         continuation = true;

      } else {

         memset(ctx, 0, sizeof(*ctx));
         sbuf_writef(sb, "\r\n");
      }
   }

   if (buf[len - 1] == '\n') {

      memset(ctx, 0, sizeof(*ctx));
      endln = "\r";

   } else if (trunc[0] == '\0') {

      ctx->last_tp_incomplete_line = 1;
      ctx->last_tp_sys_time = e->sys_time;
      ctx->last_tp_tid = e->tid;
      ctx->last_tp_in_irq = e->p_ev.in_irq ? 1 : 0;
   }

   if (len >= trunc_str_len &&
       !strcmp(buf + len - trunc_str_len, TRACE_PRINTK_TRUNC))
   {
      len -= trunc_str_len;
      trunc = E_COLOR_BR_RED TRACE_PRINTK_TRUNC RESET_ATTRS;
   }

   if (trunc[0] != '\0') {

      memset(ctx, 0, sizeof(*ctx));
      endln = "\r\n";
   }

   if (len >= 4 && !strncmp(buf, "*** ", 4))
      log_color = ATTR_BOLD;

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

/* ------------------------- top-level dispatch ------------------------- */

static void dump_tracing_event(struct sbuf *sb,
                               const struct dp_trace_event *e,
                               struct dp_render_ctx *ctx)
{
   if (e->type != dp_te_printk) {
      /* trace_printk events emit the prefix themselves only when the
       * event isn't a continuation; everything else gets it
       * unconditionally. */
      dump_event_prefix(sb, e);
   }

   switch (e->type) {

      case dp_te_sys_enter:
      case dp_te_sys_exit:
         handle_syscall_event(sb, e);
         break;

      case dp_te_printk:
         dump_trace_printk_event(sb, e, ctx);
         break;

      case dp_te_signal_delivered:
         sbuf_writef(sb,
                     E_COLOR_YELLOW "GOT SIGNAL: " RESET_ATTRS "%s[%d]\r\n",
                     tr_get_signal_name(e->sig_ev.signum),
                     e->sig_ev.signum);
         break;

      case dp_te_killed:
         sbuf_writef(sb,
                     E_COLOR_BR_RED "KILLED BY SIGNAL: "
                     RESET_ATTRS "%s[%d]\r\n",
                     tr_get_signal_name(e->sig_ev.signum),
                     e->sig_ev.signum);
         break;

      default:
         sbuf_writef(sb,
                     E_COLOR_BR_RED "<unknown event %d>\r\n" RESET_ATTRS,
                     e->type);
   }
}

int tr_render_event(const struct dp_trace_event *e,
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
