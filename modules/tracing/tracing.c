/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/mod_sysfs.h>
#include <tilck_gen_headers/config_kernel.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/modules.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/ringbuf.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/bintree.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/interrupts.h>
#include <tilck/kernel/errno.h>

#include <tilck/mods/tracing.h>
#include <tilck/common/tracing/wire.h>

#if MOD_sysfs
   #include <tilck/mods/sysfs.h>
   #include <tilck/mods/sysfs_utils.h>
#endif

/* Defined in tracing_cmd.c; registers the TILCK_CMD_DP_TRACE_* /
 * DP_TASK_* sub-commands the userspace tracer uses. */
void tracing_register_dp_cmd_handlers(void);

#define TRACE_BUF_SIZE                       (128 * KB)

struct symbol_node {

   struct bintree_node node;

   u32 sys_n;
   void *vaddr;
   const char *name;
};

static struct kcond tracing_cond;
static struct ringbuf tracing_rb;
static void *tracing_buf;

static u32 syms_count;
static struct symbol_node *syms_buf;
static struct symbol_node *syms_bintree;

static const struct syscall_info **syscalls_info;
static s8 (*params_slots)[MAX_SYSCALLS][6];
static s8 *syscalls_fmts;

STATIC char *traced_syscalls_str;
static int traced_syscalls_count;

bool *traced_syscalls;
bool __force_exp_block;
bool __tracing_on;
bool __tracing_dump_big_bufs;
bool __tracing_initialized;
bool __trace_printk_initialized;
int __tracing_printk_lvl = 10;

const char *get_signal_name(int signum)
{
   static const char *sig_names[_NSIG] =
   {
      [SIGHUP]    = "SIGHUP",
      [SIGINT]    = "SIGINT",
      [SIGQUIT]   = "SIGQUIT",
      [SIGILL]    = "SIGILL",
      [SIGABRT]   = "SIGABRT",
      [SIGFPE]    = "SIGFPE",
      [SIGKILL]   = "SIGKILL",
      [SIGSEGV]   = "SIGSEGV",
      [SIGPIPE]   = "SIGPIPE",
      [SIGALRM]   = "SIGALRM",
      [SIGTERM]   = "SIGTERM",
      [SIGUSR1]   = "SIGUSR1",
      [SIGUSR2]   = "SIGUSR2",
      [SIGCHLD]   = "SIGCHLD",
      [SIGCONT]   = "SIGCONT",
      [SIGSTOP]   = "SIGSTOP",
      [SIGTSTP]   = "SIGTSTP",
      [SIGTTIN]   = "SIGTTIN",
      [SIGTTOU]   = "SIGTTOU",
      [SIGBUS]    = "SIGBUS",
      [SIGPOLL]   = "SIGPOLL",
      [SIGPROF]   = "SIGPROF",
      [SIGSYS]    = "SIGSYS",
      [SIGTRAP]   = "SIGTRAP",
      [SIGURG]    = "SIGURG",
      [SIGVTALRM] = "SIGVTALRM",
      [SIGXCPU]   = "SIGXCPU",
      [SIGXFSZ]   = "SIGXFSZ",
      [SIGPWR]    = "SIGPWR",
      [SIGWINCH]  = "SIGWINCH",
   };

   return sig_names[signum] ? sig_names[signum] : "";
}

static int
elf_symbol_cb(struct elf_symbol_info *i, void *arg)
{
   struct symbol_node *sym_node;
   int sys_n;

   if (!i->name || strncmp(i->name, "sys_", 4))
      return 0; /* not a syscall symbol */

   if ((sys_n = get_syscall_num(i->vaddr)) < 0) {

      /*
       * Not a syscall, just a function starting with "sys_". While apparently
       * we can remove all the non-syscall functions starting with "sys_" and
       * panic in this case, unfortunately that's not possible. The reason is
       * that in optimized builds the compiler is allowed to emit multiple
       * symbols for a given function like "foo.cold.1" or "foo.hot.2" in order
       * to separate the cold paths from the hot paths. Therefore, that does
       * happen also for actual syscall functions, but we don't recognize those
       * compiler-generated symbols as syscall functions, because they are not
       * in the syscall table. For those reasons, we cannot just panic here and
       * replace this whole check with an ASSERT().
       */
      return 0;
   }

   sym_node = &syms_buf[syms_count];

   *sym_node = (struct symbol_node) {
      .sys_n = (u32) sys_n,
      .vaddr = i->vaddr,
      .name = i->name,
   };

   bintree_node_init(&sym_node->node);

   bintree_insert_ptr(&syms_bintree,
                      sym_node,
                      struct symbol_node,
                      node,
                      vaddr);

   syms_count++;
   return 0;
}

const char *
tracing_get_syscall_name(u32 n)
{
   void *ptr;
   struct symbol_node *node;

   if (!(ptr = get_syscall_func_ptr(n)))
      return NULL;

   node = bintree_find_ptr(syms_bintree, ptr, struct symbol_node, node, vaddr);

   if (!node)
      return NULL;

   return node->name;
}

int
tracing_get_param_idx(const struct syscall_info *si, const char *name)
{
   for (int i = 0; i < si->n_params; i++) {

      const struct sys_param_info *p = &si->params[i];

      /*
       * NOTE: using pointer comparison instead of strcmp() for strings.
       * This code assumes that in the metadata the same string literal will
       * be used both for the parameter name and the `helper_param_name` field.
       */
      if (p->name == name)
         return i;
   }

   return -1;
}

static void
trace_syscall_enter_save_params(const struct syscall_info *si,
                                struct trace_event *e)
{
   if (!si)
      return;

   struct syscall_event_data *se = &e->sys_ev;
   char *buf = NULL;
   size_t bs = 0;
   int idx;


   for (int i = 0; i < si->n_params; i++) {

      const struct sys_param_info *p = &si->params[i];
      const struct sys_param_type *t = p->type;

      if (t->save && (p->kind == sys_param_in || p->kind == sys_param_in_out))
      {
         long sz = -1;

         if (p->helper_param_name) {

            idx = tracing_get_param_idx(si, p->helper_param_name);
            ASSERT(idx >= 0);

            sz = (long) se->args[idx];
         }

         tracing_get_slot(e, si, i, &buf, &bs);
         ASSERT(buf && bs > 0);

         t->save(TO_PTR(se->args[i]), sz, buf, bs);
      }
   }
}

static void
trace_syscall_exit_save_params(const struct syscall_info *si,
                               struct trace_event *e)
{
   if (!si)
      return;

   struct syscall_event_data *se = &e->sys_ev;
   char *buf = NULL;
   size_t bs = 0;
   int idx;

   for (int i = 0; i < si->n_params; i++) {

      const struct sys_param_info *p = &si->params[i];
      const struct sys_param_type *t = p->type;
      const bool outp = p->kind == sys_param_out || p->kind == sys_param_in_out;

      if (t->save && (!exp_block(si) || outp))
      {
         long sz = -1;

         if (p->helper_param_name) {

            idx = tracing_get_param_idx(si, p->helper_param_name);
            ASSERT(idx >= 0);

            sz = (long) se->args[idx];
         }

         tracing_get_slot(e, si, i, &buf, &bs);
         ASSERT(buf && bs > 0);

         t->save(TO_PTR(se->args[i]), sz, buf, bs);
      }
   }
}

static void
enqueue_trace_event(struct trace_event *e)
{
   ulong var;
   bool success;
   disable_interrupts(&var);
   {
      success = ringbuf_write_elem(&tracing_rb, e);
   }
   enable_interrupts(&var);

   if (success && !in_irq()) {
      /*
       * Signal the condition only we succeeded in writing the event to our
       * ring buffer and we're not inside an IRQ handler. Not signaling a few
       * events is not a problem because by the reader cannot be stuck forever
       * and it *must* give up and retry periodically.
       */
      kcond_signal_one(&tracing_cond);
   }
}

/*
 * Public injection wrapper used by the TILCK_CMD_DP_TRACE_INJECT_EVENT
 * handler (gated by __tracing_test_mode in tracing_cmd.c). Bypasses
 * the per-task .traced / per-syscall filter / save logic: the caller
 * controls every byte of the event.
 */
void
tracing_inject_event(struct trace_event *e)
{
   enqueue_trace_event(e);
}

void
trace_syscall_enter_int(u32 sys,
                        ulong a1,
                        ulong a2,
                        ulong a3,
                        ulong a4,
                        ulong a5,
                        ulong a6)
{
   const struct syscall_info *si = tracing_get_syscall_info(sys);

   if (!get_curr_task()->traced)
      return; /* the current task is not traced */

   if (si && !exp_block(si))
      return; /* don't trace the enter event */

   struct trace_event e = {

      .type = te_sys_enter,
      .tid = get_curr_tid(),
      .sys_time = get_sys_time(),
      .sys_ev = {
         .sys = sys,
         .args = {a1,a2,a3,a4,a5,a6}
      }
   };

   trace_syscall_enter_save_params(si, &e);
   enqueue_trace_event(&e);
}

void
trace_syscall_exit_int(u32 sys,
                       long retval,
                       ulong a1,
                       ulong a2,
                       ulong a3,
                       ulong a4,
                       ulong a5,
                       ulong a6)
{
   const struct syscall_info *si = tracing_get_syscall_info(sys);

   if (!get_curr_task()->traced)
      return; /* the current task is not traced */

   struct trace_event e = {
      .type = te_sys_exit,
      .tid = get_curr_tid(),
      .sys_time = get_sys_time(),
      .sys_ev = {
         .sys = sys,
         .retval = retval,
         .args = {a1,a2,a3,a4,a5,a6}
      }
   };

   trace_syscall_exit_save_params(si, &e);
   enqueue_trace_event(&e);
}

void
trace_printk_raw_int(short level, const char *buf, size_t buf_size)
{
   ASSERT(level >= 1);

   if (!__trace_printk_initialized)
      return;

   if (__tracing_printk_lvl < level)
      return;

   struct trace_event e = {
      .type = te_printk,
      .tid = get_curr_tid(),
      .sys_time = get_sys_time(),
      .p_ev = {
         .level = level,
      }
   };

   buf_size = MIN(buf_size, sizeof(e.p_ev.buf));
   memcpy(e.p_ev.buf, buf, buf_size);
   enqueue_trace_event(&e);
}

void
trace_printk_int(short level, const char *fmt, ...)
{
   va_list args;
   int written;
   ASSERT(level >= 1);

   if (!__trace_printk_initialized)
      return;

   if (__tracing_printk_lvl < level)
      return;

   struct trace_event e = {
      .type = te_printk,
      .tid = get_curr_tid(),
      .sys_time = get_sys_time(),
      .p_ev = {
         .level = level,
         .in_irq = in_irq(),
      }
   };

   va_start(args, fmt);
   written = vsnprintk(e.p_ev.buf, sizeof(e.p_ev.buf), fmt, args);
   va_end(args);

   if (UNLIKELY(written == 0))
      return;

   if (UNLIKELY(written == (int)sizeof(e.p_ev.buf))) {

      char trunc[] = TRACE_PRINTK_TRUNC_STR;
      memcpy(e.p_ev.buf + sizeof(e.p_ev.buf) - sizeof(trunc),
             trunc,
             sizeof(trunc));

   } else if (e.p_ev.buf[written - 1] != '\n') {

      ASSERT(e.p_ev.buf[written] == '\0');
      e.p_ev.buf[written] = '\n';
      e.p_ev.buf[written + 1] = '\0';
   }

   enqueue_trace_event(&e);
}

void
trace_signal_delivered_int(int target_tid, int signum)
{
   struct task *ti;
   disable_preemption();
   {
      ti = get_task(target_tid);
   }
   enable_preemption();

   if (!ti)
      return;

   if (!ti->traced)
      return; /* the task is not traced */

   struct trace_event e = {
      .type = te_signal_delivered,
      .tid = target_tid,
      .sys_time = get_sys_time(),
      .sig_ev = {
         .signum = signum
      }
   };

   enqueue_trace_event(&e);
}

void
trace_task_killed_int(int signum)
{
   if (!get_curr_task()->traced)
      return; /* the current task is not traced */

   struct trace_event e = {
      .type = te_killed,
      .tid = get_curr_tid(),
      .sys_time = get_sys_time(),
      .sig_ev = {
         .signum = signum
      }
   };

   enqueue_trace_event(&e);
}

bool read_trace_event_noblock(struct trace_event *e)
{
   bool success;

   /* We must NOT consume trace events from IRQ handlers, of course */
   ASSERT(!in_irq());
   ASSERT(are_interrupts_enabled());
   disable_interrupts_forced();
   {
      success = ringbuf_read_elem(&tracing_rb, e);
   }
   enable_interrupts_forced();
   return success;
}

bool read_trace_event(struct trace_event *e, u32 timeout_ticks)
{
   bool success = read_trace_event_noblock(e);

   if (!success) {

      success = kcond_wait(&tracing_cond, NULL, timeout_ticks);

      if (success)
         success = read_trace_event_noblock(e);
   }

   return success;
}

const struct syscall_info *
tracing_get_syscall_info(u32 n)
{
   if (n >= MAX_SYSCALLS)
      return NULL;

   return syscalls_info[n];
}

#define NULL_TRACE_EVENT                           ((struct trace_event *)0)

#define GET_SLOT(e, fmt_n, slot_n)       ((e)->sys_ev.fmt##fmt_n.d##slot_n)

#define GET_SLOT_ABS_OFF(fmt_n, slot_n)                                    \
   ((ulong)GET_SLOT(NULL_TRACE_EVENT, fmt_n, slot_n))

#define GET_SLOT_OFF(fmt_n, slot_n)                                        \
   (GET_SLOT_ABS_OFF(fmt_n, slot_n) - GET_SLOT_ABS_OFF(fmt_n, 0))

#define GET_SLOT_SIZE(fmt_n, slot_n)                                       \
   sizeof(GET_SLOT(NULL_TRACE_EVENT, fmt_n, slot_n))

#define CAN_USE_SLOT(sys, fmt_n, slot_n, size)                             \
   (GET_SLOT_SIZE(fmt_n, slot_n) >= size && is_slot_free(sys, slot_n))

static const size_t fmt_offsets[2][4] = {

   /* fmt 0 */
   {
      GET_SLOT_ABS_OFF(0, 0),
      GET_SLOT_ABS_OFF(0, 1),
      GET_SLOT_ABS_OFF(0, 2),
      GET_SLOT_ABS_OFF(0, 3),
   },

   /* fmt 1 */
   {
      GET_SLOT_ABS_OFF(1, 0),
      GET_SLOT_ABS_OFF(1, 1),
      GET_SLOT_ABS_OFF(1, 2),
      0,
   },
};

static const size_t fmt_sizes[2][4] = {

   /* fmt 0 */
   {
      GET_SLOT_SIZE(0, 0),
      GET_SLOT_SIZE(0, 1),
      GET_SLOT_SIZE(0, 2),
      GET_SLOT_SIZE(0, 3),
   },

   /* fmt 1 */
   {
      GET_SLOT_SIZE(1, 0),
      GET_SLOT_SIZE(1, 1),
      GET_SLOT_SIZE(1, 2),
      0,
   },
};

bool
tracing_get_slot(struct trace_event *e,
                 const struct syscall_info *si,
                 int p_idx,
                 char **buf,
                 size_t *size)
{
   ASSERT(e->type == te_sys_enter || e->type == te_sys_exit);
   const s8 slot = (*params_slots)[e->sys_ev.sys][p_idx];
   s8 fmt;

   if (slot == NO_SLOT)
      return false;

   fmt = syscalls_fmts[si->sys_n];
   *buf = (char *)e + fmt_offsets[fmt][slot];
   *size = fmt_sizes[fmt][slot];
   return true;
}

static bool
is_slot_free(u32 sys, int slot)
{
   ASSERT(slot >= 0);

   for (int i = 0; i < 6; i++)
      if ((*params_slots)[sys][i] == slot)
         return false;

   return true;
}

static bool
alloc_for_fmt0(u32 sys, int p_idx, size_t size)
{
   s8 slot = NO_SLOT;

   if (!size)
      return true; /* we're fine: no need to allocate anything for this param */

   if (CAN_USE_SLOT(sys, 0, 3, size))
      slot = 3;
   else if (CAN_USE_SLOT(sys, 0, 2, size))
      slot = 2;
   else if (CAN_USE_SLOT(sys, 0, 1, size))
      slot = 1;
   else if (CAN_USE_SLOT(sys, 0, 0, size))
      slot = 0;
   else
      return false; /* we failed: another fmt should be used for this syscall */

   (*params_slots)[sys][p_idx] = slot;
   return true;     /* everything is alright */
}

static bool
alloc_for_fmt1(u32 sys, int p_idx, size_t size)
{
   s8 slot = NO_SLOT;

   if (!size)
      return true; /* we're fine: no need to allocate anything for this param */

   if (CAN_USE_SLOT(sys, 1, 2, size))
      slot = 2;
   else if (CAN_USE_SLOT(sys, 1, 1, size))
      slot = 1;
   else if (CAN_USE_SLOT(sys, 1, 0, size))
      slot = 0;
   else
      return false; /* we failed: another fmt should be used for this syscall */

   (*params_slots)[sys][p_idx] = slot;
   return true;     /* everything is alright */
}

static void
tracing_populate_syscalls_info(void)
{
   const struct syscall_info *si;

   for (si = tracing_metadata; si->sys_n != INVALID_SYSCALL; si++) {
      syscalls_info[si->sys_n] = si;
   }
}

static void
tracing_reset_slot_info(u32 sys)
{
   for (int j = 0; j < 6; j++)
      (*params_slots)[sys][j] = NO_SLOT;
}

static void
tracing_allocate_slots_for_params(void)
{
   const struct syscall_info *si;
   bool failed;

   for (u32 i = 0; i < MAX_SYSCALLS; i++)
      tracing_reset_slot_info(i);

   for (si = tracing_metadata; si->sys_n != INVALID_SYSCALL; si++) {

      const struct sys_param_info *p = si->params;
      const u32 sys_n = si->sys_n;

      failed = false;
      syscalls_fmts[sys_n] = 0; /* fmt 0 */

      for (int i = 0; i < si->n_params; i++)
         if ((failed = !alloc_for_fmt0(sys_n, i, p[i].type->slot_size)))
            break;

      if (!failed)
         continue;

      failed = false;
      syscalls_fmts[sys_n] = 1; /* fmt 1 */
      tracing_reset_slot_info(sys_n);

      for (int i = 0; i < si->n_params; i++)
         if ((failed = !alloc_for_fmt1(sys_n, i, p[i].type->slot_size)))
            break;

      if (!failed)
         continue;

      panic("Unable to alloc param slots for syscall #%u", sys_n);
   }
}

static void
debug_tracing_dump_syscalls_fmt(void)
{
   const struct syscall_info *si;
   s8 fmt;

   for (si = tracing_metadata; si->sys_n != INVALID_SYSCALL; si++) {
      fmt = syscalls_fmts[si->sys_n];
      printk("sys #%u -> fmt %d\n", si->sys_n, fmt);
   }
}

void
get_traced_syscalls_str(char *buf, size_t len)
{
   memcpy(buf, traced_syscalls_str, MIN(len, TRACED_SYSCALLS_STR_LEN));
}

int
get_traced_syscalls_count(void)
{
   return traced_syscalls_count;
}

/*
 * Minimalistic wildcard matching function.
 *
 * It supports only '*' at the end of an expression like:
 *
 *    ab*      matches:  abc, ab, abcdefgh, etc.
 *      *      matches EVERYTHING
 *
 * And the jolly character '?' which matches exactly one character (any).
 *
 * NOTE:
 *    ab*c     matches NOTHING because '*' must be the last char, if present.
 *
 */
STATIC bool
simple_wildcard_match(const char *str, const char *expr)
{
   for (; *str && *expr; str++, expr++) {

      if (*expr == '*')
         return !expr[1]; /* always fail if '*' is NOT the last char */

      if (*str != *expr && *expr != '?')
         return false; /* not a match */
   }

   /* Both `expr` and `str` ended: match */
   if (!*expr && !*str)
      return true;

   /* If `str` just ended while `expr` has just one more '*': match */
   if (*expr == '*' && !expr[1])
      return true;

   return false; /* not a match */
}

STATIC int
handle_sys_trace_arg(const char *arg)
{
   struct bintree_walk_ctx ctx;
   struct symbol_node *n;
   bool match_sign = true;

   if (!*arg)
      return 0; /* empty string */

   if (*arg == '!') {

      if (!arg[1])
         return 0; /* empty negation string */

      match_sign = false;
      arg++;
   }

   bintree_in_order_visit_start(&ctx,
                                syms_bintree,
                                struct symbol_node,
                                node,
                                false);

   while ((n = bintree_in_order_visit_next(&ctx))) {

      if (simple_wildcard_match(n->name + 4, arg))
         traced_syscalls[n->sys_n] = match_sign;
   }

   return 0;
}

STATIC int
set_traced_syscalls_int(const char *str)
{
   const size_t len = strlen(str);
   const char *s = str;
   char *p, buf[32];
   int rc;

   if (len >= TRACED_SYSCALLS_STR_LEN)
      return -ENAMETOOLONG;

   for (p = buf; *s; s++) {

      if (p == buf + sizeof(buf))
         return -ENAMETOOLONG;

      if (*s == ',' || *s == ' ') {
         *p = 0;
         p = buf;

         if ((rc = handle_sys_trace_arg(buf)))
            return rc;

         continue;
      }

      *p++ = *s;
   }

   if (p > buf) {

      *p = 0;

      if ((rc = handle_sys_trace_arg(buf)))
         return rc;
   }

   traced_syscalls_count = 0;

   for (int i = 0; i < MAX_SYSCALLS; i++)
      if (traced_syscalls[i])
         traced_syscalls_count++;

   memcpy(traced_syscalls_str, str, len + 1);
   return 0;
}

static void
reset_traced_syscalls(void)
{
   for (int i = 0; i < MAX_SYSCALLS; i++)
      traced_syscalls[i] = false;
}

int
set_traced_syscalls(const char *s)
{
   int rc;
   disable_preemption();
   {
      reset_traced_syscalls();

      if ((rc = set_traced_syscalls_int(s)))
         reset_traced_syscalls();
   }
   enable_preemption();
   return rc;
}

int
tracing_get_in_buffer_events_count(void)
{
   ulong var;
   int rc;

   disable_interrupts(&var);
   {
      rc = (int)ringbuf_get_elems(&tracing_rb); // integer narrowing
   }
   enable_interrupts(&var);
   return rc;
}

static void
tracing_init_oom_panic(const char *buf_name)
{
   panic("Unable to allocate %s in init_tracing()", buf_name);
}

/* ----------------- /syst/tracing/events stream file ------------------- */

#if MOD_sysfs

static offt
tracing_events_load(struct sysobj *obj, void *data,
                    void *buf, offt sz, offt off)
{
   if (sz < (offt)sizeof(struct trace_event))
      return -EINVAL;

   if (read_trace_event_noblock((struct trace_event *)buf))
      return (offt)sizeof(struct trace_event);

   if (pending_signals())
      return -EINTR;

   /*
    * Park for up to 100ms waiting for an event. Returning -EAGAIN on
    * timeout (rather than looping back to a fresh kcond_wait) is the
    * key to a responsive userspace tracer: the dp tool's live loop
    * polls stdin between events, but the events read parks here in
    * the kernel — without a bounded wait, a 'q' / Enter keypress
    * sits in stdin until the next event arrives. With this 100ms
    * cap, the read returns -EAGAIN ten times a second when idle, the
    * userspace loop continues, picks up the keypress, and exits.
    *
    * Mirrors the cadence the in-kernel modules/debugpanel/dp_tracing.c
    * TUI used (read_trace_event(&e, KRN_TIMER_HZ / 10)).
    */
   if (kcond_wait(&tracing_cond, NULL, KRN_TIMER_HZ / 10)) {

      /* Woken by an event — try the noblock read once more. */
      if (read_trace_event_noblock((struct trace_event *)buf))
         return (offt)sizeof(struct trace_event);
   }

   if (pending_signals())
      return -EINTR;

   return -EAGAIN;
}

static const struct sysobj_prop_type tracing_events_prop_type = {
   .buf_type = SYSFS_BUF_STREAM,
   .load     = &tracing_events_load,
};

DEF_STATIC_SYSOBJ_PROP(events, &tracing_events_prop_type);

/* ----------------- /syst/tracing/metadata immutable blob -------------- */

/*
 * Static metadata describing every traced syscall, exposed to userspace
 * as a single immutable read-only file. Built once at module init from
 * the in-memory tracing_metadata + slot allocation tables and never
 * mutated afterwards. Wire format defined in <tilck/common/tracing/wire.h>.
 *
 * The userspace tracer (dp -t / tracer) mmaps or read()s this file once
 * on startup and uses it to render trace events without needing any of
 * the kernel's struct sys_param_type / struct syscall_info layouts.
 */

static char *tracing_meta_blob;
static size_t tracing_meta_blob_size;

/*
 * Map a kernel `&ptype_X` pointer to its stable wire enum value. Single
 * source of truth lives here; userspace mirrors only the integer ids.
 */
static u8 tracing_ptype_to_id(const struct sys_param_type *t)
{
   if (t == &ptype_int)           return TR_PT_INT;
   if (t == &ptype_voidp)         return TR_PT_VOIDP;
   if (t == &ptype_oct)           return TR_PT_OCT;
   if (t == &ptype_errno_or_val)  return TR_PT_ERRNO_OR_VAL;
   if (t == &ptype_errno_or_ptr)  return TR_PT_ERRNO_OR_PTR;
   if (t == &ptype_open_flags)    return TR_PT_OPEN_FLAGS;
   if (t == &ptype_doff64)        return TR_PT_DOFF64;
   if (t == &ptype_whence)        return TR_PT_WHENCE;
   if (t == &ptype_int32_pair)    return TR_PT_INT32_PAIR;
   if (t == &ptype_u64_ptr)       return TR_PT_U64_PTR;
   if (t == &ptype_signum)        return TR_PT_SIGNUM;
   if (t == &ptype_buffer)        return TR_PT_BUFFER;
   if (t == &ptype_big_buf)       return TR_PT_BIG_BUF;
   if (t == &ptype_path)          return TR_PT_PATH;
   if (t == &ptype_iov_in)        return TR_PT_IOV_IN;
   if (t == &ptype_iov_out)       return TR_PT_IOV_OUT;

   /* Layer 1 — symbolic register-value ptypes. */
   if (t == &ptype_mmap_prot)        return TR_PT_MMAP_PROT;
   if (t == &ptype_mmap_flags)       return TR_PT_MMAP_FLAGS;
   if (t == &ptype_wait_options)     return TR_PT_WAIT_OPTIONS;
   if (t == &ptype_access_mode)      return TR_PT_ACCESS_MODE;
   if (t == &ptype_ioctl_cmd)        return TR_PT_IOCTL_CMD;
   if (t == &ptype_fcntl_cmd)        return TR_PT_FCNTL_CMD;
   if (t == &ptype_sigprocmask_how)  return TR_PT_SIGPROCMASK_HOW;
   if (t == &ptype_prctl_option)     return TR_PT_PRCTL_OPTION;
   if (t == &ptype_clone_flags)      return TR_PT_CLONE_FLAGS;
   if (t == &ptype_mount_flags)      return TR_PT_MOUNT_FLAGS;
   if (t == &ptype_madvise_advice)   return TR_PT_MADVISE_ADVICE;

   /* Layer 2 — context-dependent struct ptrs. */
   if (t == &ptype_ioctl_argp)       return TR_PT_IOCTL_ARGP;
   if (t == &ptype_fcntl_arg)        return TR_PT_FCNTL_ARG;

   /* Layer 3 — fixed struct ptrs. */
   if (t == &ptype_wstatus)          return TR_PT_WSTATUS;

   return TR_PT_NONE;
}

static void
tracing_fill_param(struct tr_wire_param *out,
                   const struct syscall_info *si,
                   const struct sys_param_info *pi)
{
   bzero(out, sizeof(*out));

   out->type_id        = pi->type ? tracing_ptype_to_id(pi->type)
                                  : TR_PT_NONE;
   out->kind           = (u8)pi->kind;
   out->invisible      = pi->invisible      ? 1 : 0;
   out->real_sz_in_ret = pi->real_sz_in_ret ? 1 : 0;
   out->helper_idx     = pi->helper_param_name
                          ? (s8)tracing_get_param_idx(si, pi->helper_param_name)
                          : -1;

   if (pi->name) {
      strncpy(out->name, pi->name, TR_PNAME_MAX - 1);
      out->name[TR_PNAME_MAX - 1] = '\0';
   }
}

static void
tracing_init_meta_blob(void)
{
   const u32 n_ptype = TR_PT_COUNT;

   /* Fixed mapping: index = enum tr_ptype_id value. */
   const struct sys_param_type *ptable[TR_PT_COUNT] = {
      [TR_PT_INT]            = &ptype_int,
      [TR_PT_VOIDP]          = &ptype_voidp,
      [TR_PT_OCT]            = &ptype_oct,
      [TR_PT_ERRNO_OR_VAL]   = &ptype_errno_or_val,
      [TR_PT_ERRNO_OR_PTR]   = &ptype_errno_or_ptr,
      [TR_PT_OPEN_FLAGS]     = &ptype_open_flags,
      [TR_PT_DOFF64]         = &ptype_doff64,
      [TR_PT_WHENCE]         = &ptype_whence,
      [TR_PT_INT32_PAIR]     = &ptype_int32_pair,
      [TR_PT_U64_PTR]        = &ptype_u64_ptr,
      [TR_PT_SIGNUM]         = &ptype_signum,
      [TR_PT_BUFFER]         = &ptype_buffer,
      [TR_PT_BIG_BUF]        = &ptype_big_buf,
      [TR_PT_PATH]           = &ptype_path,
      [TR_PT_IOV_IN]         = &ptype_iov_in,
      [TR_PT_IOV_OUT]        = &ptype_iov_out,
      [TR_PT_MMAP_PROT]       = &ptype_mmap_prot,
      [TR_PT_MMAP_FLAGS]      = &ptype_mmap_flags,
      [TR_PT_WAIT_OPTIONS]    = &ptype_wait_options,
      [TR_PT_ACCESS_MODE]     = &ptype_access_mode,
      [TR_PT_IOCTL_CMD]       = &ptype_ioctl_cmd,
      [TR_PT_FCNTL_CMD]       = &ptype_fcntl_cmd,
      [TR_PT_SIGPROCMASK_HOW] = &ptype_sigprocmask_how,
      [TR_PT_PRCTL_OPTION]    = &ptype_prctl_option,
      [TR_PT_CLONE_FLAGS]     = &ptype_clone_flags,
      [TR_PT_MOUNT_FLAGS]     = &ptype_mount_flags,
      [TR_PT_MADVISE_ADVICE]  = &ptype_madvise_advice,
      [TR_PT_IOCTL_ARGP]      = &ptype_ioctl_argp,
      [TR_PT_FCNTL_ARG]       = &ptype_fcntl_arg,
      [TR_PT_WSTATUS]         = &ptype_wstatus,
   };

   /* Count syscalls for which we have metadata (non-NULL entry in
    * syscalls_info, populated by tracing_populate_syscalls_info). */
   u32 n_sys = 0;
   for (u32 i = 0; i < MAX_SYSCALLS; i++)
      if (syscalls_info[i])
         n_sys++;

   tracing_meta_blob_size =
      sizeof(struct tr_wire_header)
      + n_ptype * sizeof(struct tr_wire_ptype_info)
      + n_sys   * sizeof(struct tr_wire_syscall);

   tracing_meta_blob = kzmalloc(tracing_meta_blob_size);

   if (!tracing_meta_blob)
      tracing_init_oom_panic("tracing_meta_blob");

   /* Header. */
   struct tr_wire_header *h = (void *)tracing_meta_blob;
   *h = (struct tr_wire_header) {
      .magic         = TR_WIRE_MAGIC,
      .version       = TR_WIRE_VERSION,
      .ptype_count   = (u16)n_ptype,
      .syscall_count = n_sys,
   };

   /* ptype info table. */
   struct tr_wire_ptype_info *pt = (void *)(h + 1);
   for (u32 i = 0; i < n_ptype; i++) {

      const struct sys_param_type *t = ptable[i];

      if (!t)
         continue; /* shouldn't happen given the static init above */

      pt[i].ui_type   = (u8)t->ui_type;
      pt[i].slot_size = (u8)t->slot_size;

      if (t->name) {
         strncpy(pt[i].name, t->name, sizeof(pt[i].name) - 1);
         pt[i].name[sizeof(pt[i].name) - 1] = '\0';
      }
   }

   /* Syscall table. */
   struct tr_wire_syscall *out = (void *)(pt + n_ptype);
   for (u32 sn = 0; sn < MAX_SYSCALLS; sn++) {

      const struct syscall_info *si = syscalls_info[sn];

      if (!si)
         continue;

      bzero(out, sizeof(*out));
      out->sys_n        = sn;
      out->n_params     = si->n_params;
      out->exp_block    = si->exp_block ? 1 : 0;
      out->ret_type_id  = si->ret_type ? tracing_ptype_to_id(si->ret_type)
                                       : TR_PT_NONE;
      out->fmt          = (u8)syscalls_fmts[sn];

      for (int p = 0; p < TR_MAX_PARAMS; p++) {
         out->slots[p] = (*params_slots)[sn][p];
         tracing_fill_param(&out->params[p], si, &si->params[p]);
      }

      out++;
   }
}

static offt
tracing_meta_get_buf_sz(struct sysobj *obj, void *data)
{
   return (offt)tracing_meta_blob_size;
}

static offt
tracing_meta_load(struct sysobj *obj, void *data,
                  void *buf, offt sz, offt off)
{
   if (off < 0 || (size_t)off > tracing_meta_blob_size)
      return -EINVAL;

   const size_t remaining = tracing_meta_blob_size - (size_t)off;
   const size_t to_copy   = MIN((size_t)sz, remaining);

   memcpy(buf, tracing_meta_blob + off, to_copy);
   return (offt)to_copy;
}

static void *
tracing_meta_get_data_ptr(struct sysobj *obj, void *data)
{
   return tracing_meta_blob;
}

static const struct sysobj_prop_type tracing_meta_prop_type = {
   .buf_type     = SYSFS_BUF_IMMUTABLE,
   .get_buf_sz   = &tracing_meta_get_buf_sz,
   .load         = &tracing_meta_load,
   .get_data_ptr = &tracing_meta_get_data_ptr,
};

DEF_STATIC_SYSOBJ_PROP(metadata, &tracing_meta_prop_type);

DEF_STATIC_SYSOBJ_TYPE(tracing_sysobj_type,
                       &prop_events,
                       &prop_metadata,
                       NULL);

static void
register_tracing_sysfs_obj(void)
{
   /*
    * The `tracing` object IS a directory (every sysobj is); each prop
    * registered on its type becomes a file under /syst/tracing/. We
    * expose two:
    *   /syst/tracing/events    -- streaming live trace events
    *   /syst/tracing/metadata  -- immutable blob, syscall metadata
    */
   struct sysobj *tracing_obj =
      sysfs_create_obj(&tracing_sysobj_type, NULL, NULL);

   if (!tracing_obj)
      return;

   if (sysfs_register_obj(NULL, &sysfs_root_obj, "tracing", tracing_obj) < 0)
      sysfs_destroy_unregistered_obj(tracing_obj);
}

#else  /* !MOD_sysfs */

/*
 * Defensive stubs: in practice MOD_tracing has a hard dep on MOD_sysfs
 * (modules/tracing/module_deps), so this branch is unreachable when
 * the tracing module is being compiled in. The stubs let the file
 * still compile if someone forces a build matrix where MOD_tracing=1
 * and MOD_sysfs=0 (which CMake will reject at configure time anyway).
 */
static void tracing_init_meta_blob(void) { /* no-op */ }
static void register_tracing_sysfs_obj(void) { /* no-op */ }

#endif

void
init_trace_printk(void)
{
   if (__trace_printk_initialized)
      return;

   if (!(tracing_buf = kzmalloc(TRACE_BUF_SIZE)))
      tracing_init_oom_panic("tracing_buf");

   ringbuf_init(&tracing_rb,
                TRACE_BUF_SIZE / sizeof(struct trace_event),
                sizeof(struct trace_event),
                tracing_buf);

   kcond_init(&tracing_cond);
   __trace_printk_initialized = true;
}

void
init_tracing(void)
{
   init_trace_printk();

   if (!(syms_buf = kalloc_array_obj(struct symbol_node, MAX_SYSCALLS)))
      tracing_init_oom_panic("syms_buf");

   if (!(syscalls_info = kzalloc_array_obj(void *, MAX_SYSCALLS)))
      tracing_init_oom_panic("syscalls_info");

   if (!(params_slots = kmalloc(sizeof(*params_slots))))
      tracing_init_oom_panic("params_slots");

   if (!(syscalls_fmts = kzalloc_array_obj(s8, MAX_SYSCALLS)))
      tracing_init_oom_panic("syscalls_fmts");

   if (!(traced_syscalls = kmalloc(MAX_SYSCALLS)))
      tracing_init_oom_panic("traced_syscalls");

   if (!(traced_syscalls_str = kmalloc(TRACED_SYSCALLS_STR_LEN)))
      tracing_init_oom_panic("traced_syscalls_str");

   foreach_symbol(elf_symbol_cb, NULL);

   tracing_populate_syscalls_info();
   tracing_allocate_slots_for_params();

   set_traced_syscalls("*");

   /* Build the immutable /syst/tracing/metadata blob from the now-
    * populated syscalls_info + params_slots + syscalls_fmts. After
    * this point the metadata is read-only by construction. */
   tracing_init_meta_blob();

   /* Expose /syst/tracing/{events,metadata} for the userspace `dp`
    * tool. MOD_tracing has a hard dep on MOD_sysfs (declared in
    * modules/tracing/module_deps), so this is unconditional. */
   register_tracing_sysfs_obj();

   /* Register the TILCK_CMD_DP_TRACE_* / DP_TASK_* sub-commands the
    * userspace tracer uses. They live in tracing_cmd.c so dp -t
    * keeps working even when MOD_debugpanel is compiled out. */
   tracing_register_dp_cmd_handlers();

   __tracing_initialized = true;
}

static struct module dp_module = {

   .name = "tracing",
   .priority = MOD_tracing_prio,
   .init = &init_tracing,
};

REGISTER_MODULE(&dp_module);
